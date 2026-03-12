#include <device/map.h>
#include <utils.h>
#include <isa.h>

#define CLINT_SIZE            0x10000
#define CLINT_MTIMECMP_OFF    0x4000
#define CLINT_MTIME_OFF       0xbff8

static uint8_t *clint_base = NULL;
static uint64_t mtimecmp = (uint64_t)-1;

static inline uint64_t clint_mtime() {
  return get_time();
}

static inline void clint_sync() {
  if (clint_mtime() >= mtimecmp) cpu.mip |= (1u << 7);
  else cpu.mip &= ~(1u << 7);
}

static void clint_io_handler(uint32_t offset, int len, bool is_write) {
  assert(len == 4 || len == 8);

  if (!is_write && offset == CLINT_MTIME_OFF) {
    *(uint64_t *)(clint_base + CLINT_MTIME_OFF) = clint_mtime();
  } else if (offset >= CLINT_MTIMECMP_OFF && offset < CLINT_MTIMECMP_OFF + 8) {
    if (is_write) {
      if (len == 8) mtimecmp = *(uint64_t *)(clint_base + CLINT_MTIMECMP_OFF);
      else {
        uint32_t part = *(uint32_t *)(clint_base + offset);
        if (offset == CLINT_MTIMECMP_OFF) mtimecmp = (mtimecmp & 0xffffffff00000000ull) | part;
        else mtimecmp = (mtimecmp & 0x00000000ffffffffull) | ((uint64_t)part << 32);
      }
    } else {
      *(uint64_t *)(clint_base + CLINT_MTIMECMP_OFF) = mtimecmp;
    }
  }

  clint_sync();
}

void xv6_clint_update() {
  clint_sync();
}

void init_xv6_clint() {
  clint_base = new_space(CLINT_SIZE);
  memset(clint_base, 0, CLINT_SIZE);
  *(uint64_t *)(clint_base + CLINT_MTIMECMP_OFF) = mtimecmp;
  add_mmio_map("xv6-clint", CONFIG_XV6_CLINT_MMIO, clint_base, CLINT_SIZE, clint_io_handler);
}
