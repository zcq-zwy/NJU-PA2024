#include <am.h>
#include <nemu.h>

void __am_disk_config(AM_DISK_CONFIG_T *cfg) {
  cfg->blksz = inl(DISK_BLKSZ_ADDR);
  cfg->blkcnt = inl(DISK_BLKCNT_ADDR);
  cfg->present = (cfg->blksz != 0 && cfg->blkcnt != 0);
}

void __am_disk_status(AM_DISK_STATUS_T *stat) {
  stat->ready = inl(DISK_STATUS_ADDR);
}

void __am_disk_blkio(AM_DISK_BLKIO_T *io) {
  outl(DISK_BUF_LO_ADDR, (uint32_t)(uintptr_t)io->buf);
  outl(DISK_BUF_HI_ADDR, (uint32_t)((uint64_t)(uintptr_t)io->buf >> 32));
  outl(DISK_BLKNO_ADDR, io->blkno);
  outl(DISK_BLKIO_CNT_ADDR, io->blkcnt);
  outl(DISK_CMD_ADDR, io->write ? DISK_CMD_WRITE : DISK_CMD_READ);
}
