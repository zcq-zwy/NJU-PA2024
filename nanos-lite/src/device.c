#include <common.h>

#if defined(MULTIPROGRAM) && !defined(TIME_SHARING)
# define MULTIPROGRAM_YIELD() yield()
#else
# define MULTIPROGRAM_YIELD()
#endif

#define NAME(key)   [AM_KEY_##key] = #key,

static const char *keyname[256] __attribute__((used)) = {
  [AM_KEY_NONE] = "NONE",
  AM_KEYS(NAME)
};

size_t serial_write(const void *buf, size_t offset, size_t len) {
  (void)offset;
  const char *p = (const char *)buf;
  for (size_t i = 0; i < len; i++) {
    putch(p[i]);
  }
  return len;
}

size_t events_read(void *buf, size_t offset, size_t len) {
  (void)offset;
  if (len == 0) return 0;

  AM_INPUT_KEYBRD_T ev = io_read(AM_INPUT_KEYBRD);
  if (ev.keycode == AM_KEY_NONE) return 0;

  char event[64];
  int n = snprintf(event, sizeof(event), "k%c %s\n", ev.keydown ? 'd' : 'u', keyname[ev.keycode]);
  if (n <= 0) return 0;

  size_t copy_len = n < len ? n : len;
  memcpy(buf, event, copy_len);
  return copy_len;
}

size_t dispinfo_read(void *buf, size_t offset, size_t len) {
  (void)offset;
  if (len == 0) return 0;

  AM_GPU_CONFIG_T gpu = io_read(AM_GPU_CONFIG);
  char info[64];
  int n = snprintf(info, sizeof(info), "WIDTH: %d\nHEIGHT: %d\n", gpu.width, gpu.height);
  if (n <= 0) return 0;

  size_t copy_len = n < len ? n : len;
  memcpy(buf, info, copy_len);
  return copy_len;
}

size_t fb_write(const void *buf, size_t offset, size_t len) {
  if (len == 0) return 0;

  AM_GPU_CONFIG_T gpu = io_read(AM_GPU_CONFIG);
  size_t pixel_offset = offset / sizeof(uint32_t);
  int x = pixel_offset % gpu.width;
  int y = pixel_offset / gpu.width;
  int w = len / sizeof(uint32_t);

  io_write(AM_GPU_FBDRAW, x, y, (void *)buf, w, 1, true);
  return len;
}

void init_device() {
  Log("Initializing devices...");
  ioe_init();
}
