#include <fs.h>

typedef size_t (*ReadFn) (void *buf, size_t offset, size_t len);
typedef size_t (*WriteFn) (const void *buf, size_t offset, size_t len);

typedef struct {
  char *name;
  size_t size;
  size_t disk_offset;
  ReadFn read;
  WriteFn write;
  size_t open_offset;
} Finfo;

enum {FD_STDIN, FD_STDOUT, FD_STDERR, FD_EVENTS, FD_DISPINFO, FD_FB, FD_SBCTL, FD_SB};

enum { FS_ENOENT = 2 };

size_t serial_write(const void *buf, size_t offset, size_t len);
size_t events_read(void *buf, size_t offset, size_t len);
size_t dispinfo_read(void *buf, size_t offset, size_t len);
size_t fb_write(const void *buf, size_t offset, size_t len);
size_t sbctl_read(void *buf, size_t offset, size_t len);
size_t sbctl_write(const void *buf, size_t offset, size_t len);
size_t sb_write(const void *buf, size_t offset, size_t len);
size_t ramdisk_read(void *buf, size_t offset, size_t len);
size_t ramdisk_write(const void *buf, size_t offset, size_t len);

static size_t invalid_read(void *buf, size_t offset, size_t len) {
  panic("should not reach here");
  return 0;
}

static size_t invalid_write(const void *buf, size_t offset, size_t len) {
  panic("should not reach here");
  return 0;
}

static Finfo file_table[] __attribute__((used)) = {
  [FD_STDIN]    = {"stdin",          0, 0, invalid_read, invalid_write, 0},
  [FD_STDOUT]   = {"stdout",         0, 0, NULL,         serial_write,  0},
  [FD_STDERR]   = {"stderr",         0, 0, NULL,         serial_write,  0},
  [FD_EVENTS]   = {"/dev/events",    0, 0, events_read,  invalid_write, 0},
  [FD_DISPINFO] = {"/proc/dispinfo", 0, 0, dispinfo_read,invalid_write, 0},
  [FD_FB]       = {"/dev/fb",        0, 0, invalid_read, fb_write,      0},
  [FD_SBCTL]    = {"/dev/sbctl",     0, 0, sbctl_read,   sbctl_write,   0},
  [FD_SB]       = {"/dev/sb",        0, 0, invalid_read, sb_write,      0},
#include "files.h"
};

void init_fs() {
  AM_GPU_CONFIG_T gpu = io_read(AM_GPU_CONFIG);
  file_table[FD_FB].size = gpu.vmemsz;
}

int fs_open(const char *pathname, int flags, int mode) {
  (void)flags;
  (void)mode;

  for (int i = 0; i < LENGTH(file_table); i++) {
    if (strcmp(file_table[i].name, pathname) == 0) {
      file_table[i].open_offset = 0;
      return i;
    }
  }

  return -FS_ENOENT;
}

size_t fs_read(int fd, void *buf, size_t len) {
  Finfo *f = &file_table[fd];

  size_t ret = 0;
  if (f->read != NULL) {
    ret = f->read(buf, f->open_offset, len);
  } else {
    size_t remain = (f->size > f->open_offset ? f->size - f->open_offset : 0);
    if (len > remain) len = remain;
    ret = ramdisk_read(buf, f->disk_offset + f->open_offset, len);
  }
  f->open_offset += ret;
  return ret;
}

size_t fs_write(int fd, const void *buf, size_t len) {
  Finfo *f = &file_table[fd];
  size_t ret = 0;

  if (f->write != NULL) {
    ret = f->write(buf, f->open_offset, len);
  } else {
    size_t remain = (f->size > f->open_offset ? f->size - f->open_offset : 0);
    if (len > remain) len = remain;
    ret = ramdisk_write(buf, f->disk_offset + f->open_offset, len);
  }

  f->open_offset += ret;
  return ret;
}

size_t fs_lseek(int fd, size_t offset, int whence) {
  Finfo *f = &file_table[fd];

  switch (whence) {
    case SEEK_SET: f->open_offset = offset; break;
    case SEEK_CUR: f->open_offset += offset; break;
    case SEEK_END: f->open_offset = f->size + offset; break;
    default: panic("invalid whence = %d", whence);
  }

  return f->open_offset;
}

int fs_close(int fd) {
  file_table[fd].open_offset = 0;
  return 0;
}

const char *fs_get_filename(int fd) {
  if (fd < 0 || fd >= LENGTH(file_table)) return "invalid-fd";
  return file_table[fd].name;
}
