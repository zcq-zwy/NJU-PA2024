#include <fs.h>

typedef size_t (*ReadFn) (void *buf, size_t offset, size_t len);
typedef size_t (*WriteFn) (const void *buf, size_t offset, size_t len);

typedef struct {
  char *name;
  size_t size;
  size_t disk_offset;
  ReadFn read;
  WriteFn write;
} Finfo;

typedef struct {
  int file_index;
  size_t open_offset;
  bool used;
} FdInfo;

enum {FD_STDIN, FD_STDOUT, FD_STDERR, FD_EVENTS, FD_DISPINFO, FD_SYSINFO, FD_FB, FD_SBCTL, FD_SB};

enum { FS_ENOENT = 2, FS_EMFILE = 24, NR_OPEN_FILES = 64 };
enum { NR_RESERVED_FD = FD_SB + 1 };

size_t serial_write(const void *buf, size_t offset, size_t len);
size_t events_read(void *buf, size_t offset, size_t len);
size_t dispinfo_read(void *buf, size_t offset, size_t len);
size_t sysinfo_read(void *buf, size_t offset, size_t len);
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
  [FD_STDIN]    = {"stdin",          0, 0, invalid_read, invalid_write},
  [FD_STDOUT]   = {"stdout",         0, 0, NULL,         serial_write },
  [FD_STDERR]   = {"stderr",         0, 0, NULL,         serial_write },
  [FD_EVENTS]   = {"/dev/events",    0, 0, events_read,  invalid_write},
  [FD_DISPINFO] = {"/proc/dispinfo", 0, 0, dispinfo_read,invalid_write},
  [FD_SYSINFO]  = {"/proc/sysinfo",  0, 0, sysinfo_read, invalid_write},
  [FD_FB]       = {"/dev/fb",        0, 0, invalid_read, fb_write     },
  [FD_SBCTL]    = {"/dev/sbctl",     0, 0, sbctl_read,   sbctl_write  },
  [FD_SB]       = {"/dev/sb",        0, 0, invalid_read, sb_write     },
#include "files.h"
};

static FdInfo fd_table[NR_OPEN_FILES];

static inline FdInfo *get_fdinfo(int fd) {
  assert(fd >= 0 && fd < NR_OPEN_FILES && fd_table[fd].used);
  return &fd_table[fd];
}

void init_fs() {
  AM_GPU_CONFIG_T gpu = io_read(AM_GPU_CONFIG);
  file_table[FD_FB].size = gpu.vmemsz;

  for (int i = 0; i < NR_OPEN_FILES; i++) {
    fd_table[i].used = false;
    fd_table[i].file_index = -1;
    fd_table[i].open_offset = 0;
  }

  for (int i = 0; i < NR_RESERVED_FD; i++) {
    fd_table[i].used = true;
    fd_table[i].file_index = i;
  }
}

int fs_open(const char *pathname, int flags, int mode) {
  (void)flags;
  (void)mode;

  for (int file = 0; file < LENGTH(file_table); file++) {
    if (strcmp(file_table[file].name, pathname) == 0) {
      for (int fd = NR_RESERVED_FD; fd < NR_OPEN_FILES; fd++) {
        if (!fd_table[fd].used) {
          fd_table[fd].used = true;
          fd_table[fd].file_index = file;
          fd_table[fd].open_offset = 0;
          return fd;
        }
      }
      return -FS_EMFILE;
    }
  }

  return -FS_ENOENT;
}

size_t fs_read(int fd, void *buf, size_t len) {
  FdInfo *fi = get_fdinfo(fd);
  Finfo *f = &file_table[fi->file_index];

  size_t ret = 0;
  if (f->read != NULL) {
    ret = f->read(buf, fi->open_offset, len);
  } else {
    size_t remain = (f->size > fi->open_offset ? f->size - fi->open_offset : 0);
    if (len > remain) len = remain;
    ret = ramdisk_read(buf, f->disk_offset + fi->open_offset, len);
  }
  fi->open_offset += ret;
  return ret;
}

size_t fs_write(int fd, const void *buf, size_t len) {
  FdInfo *fi = get_fdinfo(fd);
  Finfo *f = &file_table[fi->file_index];
  size_t ret = 0;

  if (f->write != NULL) {
    ret = f->write(buf, fi->open_offset, len);
  } else {
    size_t remain = (f->size > fi->open_offset ? f->size - fi->open_offset : 0);
    if (len > remain) len = remain;
    ret = ramdisk_write(buf, f->disk_offset + fi->open_offset, len);
  }

  fi->open_offset += ret;
  return ret;
}

size_t fs_lseek(int fd, size_t offset, int whence) {
  FdInfo *fi = get_fdinfo(fd);
  Finfo *f = &file_table[fi->file_index];

  switch (whence) {
    case SEEK_SET: fi->open_offset = offset; break;
    case SEEK_CUR: fi->open_offset += offset; break;
    case SEEK_END: fi->open_offset = f->size + offset; break;
    default: panic("invalid whence = %d", whence);
  }

  return fi->open_offset;
}

int fs_close(int fd) {
  FdInfo *fi = get_fdinfo(fd);
  if (fd >= NR_RESERVED_FD) {
    fi->used = false;
    fi->file_index = -1;
  }
  fi->open_offset = 0;
  return 0;
}

const char *fs_get_filename(int fd) {
  if (fd < 0 || fd >= NR_OPEN_FILES || !fd_table[fd].used) return "invalid-fd";
  return file_table[fd_table[fd].file_index].name;
}

size_t fs_storage_used_bytes(void) {
  size_t used = 0;
  for (int i = NR_RESERVED_FD; i < LENGTH(file_table); i++) {
    size_t end = file_table[i].disk_offset + file_table[i].size;
    if (end > used) used = end;
  }
  return used;
}

size_t fs_storage_total_bytes(void) {
  extern size_t get_ramdisk_size(void);
  return get_ramdisk_size();
}
