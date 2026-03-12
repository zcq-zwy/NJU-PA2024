/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <device/map.h>
#include <memory/paddr.h>
#include <stdlib.h>

#define DISK_BLKSZ 512

enum {
  reg_blksz,
  reg_blkcnt,
  reg_buf_lo,
  reg_buf_hi,
  reg_blkno,
  reg_nblk,
  reg_cmd,
  reg_status,
  nr_reg
};

enum {
  disk_cmd_none = 0,
  disk_cmd_read = 1,
  disk_cmd_write = 2,
};

static uint32_t *disk_base = NULL;
static FILE *disk_fp = NULL;

static const char *get_disk_path(void) {
  static char path[1024];
  const char *navy_home = getenv("NAVY_HOME");
  if (navy_home != NULL && navy_home[0] != '\0') {
    snprintf(path, sizeof(path), "%s/build/ramdisk.img", navy_home);
    return path;
  }
  if (CONFIG_DISK_IMG_PATH[0] != '\0') {
    return CONFIG_DISK_IMG_PATH;
  }
  return NULL;
}

static void do_disk_cmd(uint32_t cmd) {
  if (disk_fp == NULL) return;

  uint64_t buf64 = ((uint64_t)disk_base[reg_buf_hi] << 32) | disk_base[reg_buf_lo];
  paddr_t buf = (paddr_t)buf64;
  uint32_t blkno = disk_base[reg_blkno];
  uint32_t nblk = disk_base[reg_nblk];
  size_t nbytes = (size_t)nblk * DISK_BLKSZ;

  assert(nblk > 0);
  assert(in_pmem(buf));
  assert(in_pmem(buf + nbytes - 1));

  uint8_t *hbuf = guest_to_host(buf);
  int ret = fseek(disk_fp, (long)blkno * DISK_BLKSZ, SEEK_SET);
  assert(ret == 0);

  if (cmd == disk_cmd_read) {
    ret = fread(hbuf, nbytes, 1, disk_fp);
    assert(ret == 1);
  } else if (cmd == disk_cmd_write) {
    ret = fwrite(hbuf, nbytes, 1, disk_fp);
    assert(ret == 1);
    ret = fflush(disk_fp);
    assert(ret == 0);
  }
}

static void disk_io_handler(uint32_t offset, int len, bool is_write) {
  assert(len == 4);
  int reg = offset / sizeof(uint32_t);
  assert(reg >= 0 && reg < nr_reg);

  if (is_write && reg == reg_cmd) {
    uint32_t cmd = disk_base[reg_cmd];
    disk_base[reg_status] = 0;
    do_disk_cmd(cmd);
    disk_base[reg_cmd] = disk_cmd_none;
    disk_base[reg_status] = 1;
  }
}

void init_disk() {
  disk_base = (uint32_t *)new_space(sizeof(uint32_t) * nr_reg);
  memset(disk_base, 0, sizeof(uint32_t) * nr_reg);
  disk_base[reg_blksz] = DISK_BLKSZ;
  disk_base[reg_status] = 1;

  const char *disk_path = get_disk_path();
  if (disk_path != NULL) {
    disk_fp = fopen(disk_path, "r+");
    Assert(disk_fp != NULL, "Can not open disk image '%s'", disk_path);
    int ret = fseek(disk_fp, 0, SEEK_END);
    assert(ret == 0);
    long size = ftell(disk_fp);
    assert(size >= 0);
    rewind(disk_fp);
    disk_base[reg_blkcnt] = size / DISK_BLKSZ;
    Log("disk image: %s, size = %ld bytes", disk_path, size);
  }

#ifdef CONFIG_HAS_PORT_IO
  add_pio_map("disk", CONFIG_DISK_CTL_PORT, disk_base, sizeof(uint32_t) * nr_reg, disk_io_handler);
#else
  add_mmio_map("disk", CONFIG_DISK_CTL_MMIO, disk_base, sizeof(uint32_t) * nr_reg, disk_io_handler);
#endif
}
