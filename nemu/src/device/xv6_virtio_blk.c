#include <device/map.h>
#include <memory/paddr.h>
#include <stdlib.h>
#include <unistd.h>

#define VIRTIO_MMIO_MAGIC_VALUE       0x000
#define VIRTIO_MMIO_VERSION           0x004
#define VIRTIO_MMIO_DEVICE_ID         0x008
#define VIRTIO_MMIO_VENDOR_ID         0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES   0x010
#define VIRTIO_MMIO_DRIVER_FEATURES   0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE   0x028
#define VIRTIO_MMIO_QUEUE_SEL         0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX     0x034
#define VIRTIO_MMIO_QUEUE_NUM         0x038
#define VIRTIO_MMIO_QUEUE_ALIGN       0x03c
#define VIRTIO_MMIO_QUEUE_PFN         0x040
#define VIRTIO_MMIO_QUEUE_READY       0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY      0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS  0x060
#define VIRTIO_MMIO_INTERRUPT_ACK     0x064
#define VIRTIO_MMIO_STATUS            0x070

#define VIRTIO_MMIO_SIZE              0x1000
#define VIRTIO_VENDOR_QEMU            0x554d4551u
#define VIRTIO_DEVICE_BLOCK           2u
#define VIRTIO_VERSION_LEGACY         1u
#define VIRTIO_BLK_T_IN               0u
#define VIRTIO_BLK_T_OUT              1u
#define VRING_DESC_F_NEXT             1u
#define VRING_DESC_F_WRITE            2u
#define SECTOR_SIZE                   512u

typedef struct {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed)) VRingDesc;

typedef struct {
  uint32_t type;
  uint32_t reserved;
  uint64_t sector;
} __attribute__((packed)) VirtioBlkOuthdr;

typedef struct {
  uint32_t id;
  uint32_t len;
} __attribute__((packed)) VRingUsedElem;

typedef struct {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[];
} __attribute__((packed)) VRingAvail;

typedef struct {
  uint16_t flags;
  uint16_t idx;
  VRingUsedElem ring[];
} __attribute__((packed)) VRingUsed;

static uint32_t *virtio_base = NULL;
static FILE *disk_fp = NULL;
static uint16_t last_avail_idx = 0;

void xv6_plic_raise_irq(int irq);

static const char *get_disk_path(void) {
  static char path[1024];
  const char *xv6_home = getenv("XV6_HOME");
  if (xv6_home != NULL && xv6_home[0] != '\0') {
    snprintf(path, sizeof(path), "%s/fs.img", xv6_home);
    if (access(path, R_OK | W_OK) == 0) return path;
  }
  if (access("/root/ics2024/third_party/xv6-rv32/fs.img", R_OK | W_OK) == 0) {
    return "/root/ics2024/third_party/xv6-rv32/fs.img";
  }
  const char *navy_home = getenv("NAVY_HOME");
  if (navy_home != NULL && navy_home[0] != '\0') {
    snprintf(path, sizeof(path), "%s/build/ramdisk.img", navy_home);
    return path;
  }
  if (CONFIG_DISK_IMG_PATH[0] != '\0') return CONFIG_DISK_IMG_PATH;
  return NULL;
}

static inline uint32_t reg_read32(uint32_t off) {
  return virtio_base[off / sizeof(uint32_t)];
}

static inline paddr_t queue_pa(void) {
  uint32_t page_size = reg_read32(VIRTIO_MMIO_GUEST_PAGE_SIZE);
  if (page_size == 0) page_size = 4096;
  return (paddr_t)reg_read32(VIRTIO_MMIO_QUEUE_PFN) * page_size;
}

static inline uint32_t queue_align(void) {
  uint32_t align = reg_read32(VIRTIO_MMIO_QUEUE_ALIGN);
  return align == 0 ? 4096 : align;
}

static inline uint32_t queue_num(void) {
  uint32_t num = reg_read32(VIRTIO_MMIO_QUEUE_NUM);
  return num == 0 ? 8 : num;
}

static void process_chain(uint16_t head) {
  paddr_t qpa = queue_pa();
  uint32_t qnum = queue_num();
  uint32_t qalign = queue_align();
  assert(qpa != 0);
  assert(qnum > 0 && qnum <= 8);

  VRingDesc *desc = (VRingDesc *)guest_to_host(qpa);
  VRingUsed *used = (VRingUsed *)guest_to_host(qpa + qalign);
  assert(head < qnum);

  VRingDesc *d0 = &desc[head];
  assert(d0->flags & VRING_DESC_F_NEXT);
  VRingDesc *d1 = &desc[d0->next];
  VRingDesc *d2 = &desc[d1->next];

  VirtioBlkOuthdr *hdr = (VirtioBlkOuthdr *)guest_to_host((paddr_t)d0->addr);
  uint8_t *data = guest_to_host((paddr_t)d1->addr);
  uint8_t *status = guest_to_host((paddr_t)d2->addr);

  int ret = fseek(disk_fp, (long)(hdr->sector * SECTOR_SIZE), SEEK_SET);
  assert(ret == 0);
  if (hdr->type == VIRTIO_BLK_T_IN) {
    ret = fread(data, d1->len, 1, disk_fp);
    assert(ret == 1);
  } else if (hdr->type == VIRTIO_BLK_T_OUT) {
    ret = fwrite(data, d1->len, 1, disk_fp);
    assert(ret == 1);
    ret = fflush(disk_fp);
    assert(ret == 0);
  } else {
    panic("unsupported virtio blk request type = %u", hdr->type);
  }

  *status = 0;
  uint16_t used_idx = used->idx % qnum;
  used->ring[used_idx].id = head;
  used->ring[used_idx].len = d1->len;
  __sync_synchronize();
  used->idx++;
}

static void process_queue(void) {
  paddr_t qpa = queue_pa();
  uint32_t qnum = queue_num();
  assert(qpa != 0);

  VRingAvail *avail = (VRingAvail *)guest_to_host(qpa + sizeof(VRingDesc) * qnum);
  while (last_avail_idx != avail->idx) {
    uint16_t head = avail->ring[last_avail_idx % qnum];
    process_chain(head);
    last_avail_idx++;
  }

  virtio_base[VIRTIO_MMIO_INTERRUPT_STATUS / sizeof(uint32_t)] |= 0x1;
  xv6_plic_raise_irq(CONFIG_XV6_VIRTIO_BLK_IRQ);
}

static void virtio_blk_io_handler(uint32_t offset, int len, bool is_write) {
  assert(len == 4);
  switch (offset) {
    case VIRTIO_MMIO_MAGIC_VALUE:
      if (!is_write) virtio_base[offset / 4] = 0x74726976u;
      break;
    case VIRTIO_MMIO_VERSION:
      if (!is_write) virtio_base[offset / 4] = VIRTIO_VERSION_LEGACY;
      break;
    case VIRTIO_MMIO_DEVICE_ID:
      if (!is_write) virtio_base[offset / 4] = VIRTIO_DEVICE_BLOCK;
      break;
    case VIRTIO_MMIO_VENDOR_ID:
      if (!is_write) virtio_base[offset / 4] = VIRTIO_VENDOR_QEMU;
      break;
    case VIRTIO_MMIO_DEVICE_FEATURES:
      if (!is_write) virtio_base[offset / 4] = 0;
      break;
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
      if (!is_write) virtio_base[offset / 4] = 8;
      break;
    case VIRTIO_MMIO_QUEUE_NOTIFY:
      if (is_write) process_queue();
      break;
    case VIRTIO_MMIO_INTERRUPT_ACK:
      if (is_write) {
        virtio_base[VIRTIO_MMIO_INTERRUPT_STATUS / 4] &= ~virtio_base[offset / 4];
      }
      break;
    case VIRTIO_MMIO_STATUS:
      if (is_write && virtio_base[offset / 4] == 0) {
        last_avail_idx = 0;
      }
      break;
    default:
      break;
  }
}

void init_xv6_virtio_blk() {
  virtio_base = (uint32_t *)new_space(VIRTIO_MMIO_SIZE);
  memset(virtio_base, 0, VIRTIO_MMIO_SIZE);
  virtio_base[VIRTIO_MMIO_QUEUE_NUM_MAX / 4] = 8;
  virtio_base[VIRTIO_MMIO_QUEUE_READY / 4] = 1;

  const char *disk_path = get_disk_path();
  Assert(disk_path != NULL, "xv6 virtio disk image path is not set");
  disk_fp = fopen(disk_path, "r+");
  Assert(disk_fp != NULL, "Can not open xv6 virtio disk image '%s'", disk_path);

  add_mmio_map("xv6-virtio-blk", CONFIG_XV6_VIRTIO_BLK_MMIO, virtio_base, VIRTIO_MMIO_SIZE, virtio_blk_io_handler);
}
