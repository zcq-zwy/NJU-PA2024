#include <sdl-file.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static inline int rw_fd(SDL_RWops *f) {
  return (int)(intptr_t)f->fp;
}

static int64_t rw_file_size(SDL_RWops *f) {
  int fd = rw_fd(f);
  off_t cur = lseek(fd, 0, SEEK_CUR);
  off_t size = lseek(fd, 0, SEEK_END);
  lseek(fd, cur, SEEK_SET);
  return size;
}

static int64_t rw_file_seek(SDL_RWops *f, int64_t offset, int whence) {
  return lseek(rw_fd(f), offset, whence);
}

static size_t rw_file_read(SDL_RWops *f, void *buf, size_t size, size_t nmemb) {
  if (size == 0) return 0;
  ssize_t ret = read(rw_fd(f), buf, size * nmemb);
  if (ret <= 0) return 0;
  return ret / size;
}

static size_t rw_file_write(SDL_RWops *f, const void *buf, size_t size, size_t nmemb) {
  if (size == 0) return 0;
  ssize_t ret = write(rw_fd(f), buf, size * nmemb);
  if (ret <= 0) return 0;
  return ret / size;
}

static int rw_file_close(SDL_RWops *f) {
  int ret = close(rw_fd(f));
  free(f);
  return ret;
}

static int64_t rw_mem_pos(SDL_RWops *f) {
  return (int64_t)(intptr_t)f->fp;
}

static void rw_mem_set_pos(SDL_RWops *f, int64_t pos) {
  f->fp = (FILE *)(intptr_t)pos;
}

static int64_t rw_mem_size(SDL_RWops *f) {
  return f->mem.size;
}

static int64_t rw_mem_seek(SDL_RWops *f, int64_t offset, int whence) {
  int64_t newpos = 0;
  switch (whence) {
    case RW_SEEK_SET: newpos = offset; break;
    case RW_SEEK_CUR: newpos = rw_mem_pos(f) + offset; break;
    case RW_SEEK_END: newpos = f->mem.size + offset; break;
    default: return -1;
  }
  if (newpos < 0) newpos = 0;
  if (newpos > f->mem.size) newpos = f->mem.size;
  rw_mem_set_pos(f, newpos);
  return newpos;
}

static size_t rw_mem_read(SDL_RWops *f, void *buf, size_t size, size_t nmemb) {
  if (size == 0) return 0;
  int64_t pos = rw_mem_pos(f);
  int64_t remain = f->mem.size - pos;
  size_t max_nmemb = remain > 0 ? (size_t)(remain / (int64_t)size) : 0;
  if (nmemb > max_nmemb) nmemb = max_nmemb;
  size_t total = size * nmemb;
  if (total > 0) {
    memcpy(buf, (uint8_t *)f->mem.base + pos, total);
    rw_mem_set_pos(f, pos + total);
  }
  return nmemb;
}

static size_t rw_mem_write(SDL_RWops *f, const void *buf, size_t size, size_t nmemb) {
  if (size == 0) return 0;
  int64_t pos = rw_mem_pos(f);
  int64_t remain = f->mem.size - pos;
  size_t max_nmemb = remain > 0 ? (size_t)(remain / (int64_t)size) : 0;
  if (nmemb > max_nmemb) nmemb = max_nmemb;
  size_t total = size * nmemb;
  if (total > 0) {
    memcpy((uint8_t *)f->mem.base + pos, buf, total);
    rw_mem_set_pos(f, pos + total);
  }
  return nmemb;
}

static int rw_mem_close(SDL_RWops *f) {
  free(f);
  return 0;
}

SDL_RWops* SDL_RWFromFile(const char *filename, const char *mode) {
  int flags = O_RDONLY;
  if (strcmp(mode, "r+") == 0 || strcmp(mode, "rb+") == 0 || strcmp(mode, "r+b") == 0) flags = O_RDWR;
  else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) flags = O_WRONLY | O_CREAT | O_TRUNC;
  else if (strcmp(mode, "w+") == 0 || strcmp(mode, "wb+") == 0 || strcmp(mode, "w+b") == 0) flags = O_RDWR | O_CREAT | O_TRUNC;

  int fd = open(filename, flags, 0);
  if (fd < 0) return NULL;

  SDL_RWops *rw = (SDL_RWops *)malloc(sizeof(SDL_RWops));
  if (rw == NULL) {
    close(fd);
    return NULL;
  }

  rw->size = rw_file_size;
  rw->seek = rw_file_seek;
  rw->read = rw_file_read;
  rw->write = rw_file_write;
  rw->close = rw_file_close;
  rw->type = RW_TYPE_FILE;
  rw->fp = (FILE *)(intptr_t)fd;
  rw->mem.base = NULL;
  rw->mem.size = 0;
  return rw;
}

SDL_RWops* SDL_RWFromMem(void *mem, int size) {
  SDL_RWops *rw = (SDL_RWops *)malloc(sizeof(SDL_RWops));
  if (rw == NULL) return NULL;

  rw->size = rw_mem_size;
  rw->seek = rw_mem_seek;
  rw->read = rw_mem_read;
  rw->write = rw_mem_write;
  rw->close = rw_mem_close;
  rw->type = RW_TYPE_MEM;
  rw->fp = (FILE *)(intptr_t)0;
  rw->mem.base = mem;
  rw->mem.size = size;
  return rw;
}
