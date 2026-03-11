#include <common.h>
#include <am.h>

static bool disk_present = false;
static size_t disk_blksz = 0;
static size_t disk_blkcnt = 0;
static uint8_t bounce[4096];

static inline size_t disk_total_bytes(void) {
  return disk_blksz * disk_blkcnt;
}

bool disk_available(void) {
  return disk_present;
}

size_t get_disk_size(void) {
  return disk_total_bytes();
}

size_t disk_read(void *buf, size_t offset, size_t len) {
  assert(disk_present);
  assert(offset + len <= disk_total_bytes());

  uint8_t *dst = (uint8_t *)buf;
  size_t done = 0;
  while (done < len) {
    size_t cur = offset + done;
    size_t blkno = cur / disk_blksz;
    size_t blkoff = cur % disk_blksz;
    size_t chunk = len - done;
    if (chunk > disk_blksz - blkoff) chunk = disk_blksz - blkoff;

    if (blkoff == 0 && chunk == disk_blksz) {
      io_write(AM_DISK_BLKIO, .write = false, .buf = dst + done, .blkno = blkno, .blkcnt = 1);
    } else {
      io_write(AM_DISK_BLKIO, .write = false, .buf = bounce, .blkno = blkno, .blkcnt = 1);
      memcpy(dst + done, bounce + blkoff, chunk);
    }
    done += chunk;
  }
  return len;
}

size_t disk_write(const void *buf, size_t offset, size_t len) {
  assert(disk_present);
  assert(offset + len <= disk_total_bytes());

  const uint8_t *src = (const uint8_t *)buf;
  size_t done = 0;
  while (done < len) {
    size_t cur = offset + done;
    size_t blkno = cur / disk_blksz;
    size_t blkoff = cur % disk_blksz;
    size_t chunk = len - done;
    if (chunk > disk_blksz - blkoff) chunk = disk_blksz - blkoff;

    if (blkoff == 0 && chunk == disk_blksz) {
      io_write(AM_DISK_BLKIO, .write = true, .buf = (void *)(src + done), .blkno = blkno, .blkcnt = 1);
    } else {
      io_write(AM_DISK_BLKIO, .write = false, .buf = bounce, .blkno = blkno, .blkcnt = 1);
      memcpy(bounce + blkoff, src + done, chunk);
      io_write(AM_DISK_BLKIO, .write = true, .buf = bounce, .blkno = blkno, .blkcnt = 1);
    }
    done += chunk;
  }
  return len;
}

void init_disk(void) {
  AM_DISK_CONFIG_T cfg = io_read(AM_DISK_CONFIG);
  disk_present = cfg.present;
  if (!disk_present) {
    panic("disk image is not present, please pass diskimg=...");
  }

  disk_blksz = cfg.blksz;
  disk_blkcnt = cfg.blkcnt;
  assert(disk_blksz > 0 && disk_blkcnt > 0);
  assert(disk_blksz <= sizeof(bounce));

  AM_DISK_STATUS_T status = io_read(AM_DISK_STATUS);
  assert(status.ready);

  Log("disk info: blksz = %d, blkcnt = %d, size = %d bytes",
      (int)disk_blksz, (int)disk_blkcnt, (int)disk_total_bytes());
}
