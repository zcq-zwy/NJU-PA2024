#include <common.h>
#include <proc.h>

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

  if (ev.keydown) {
    switch (ev.keycode) {
      case AM_KEY_F1: switch_fg_pcb(PCB_PAL); return 0;
      case AM_KEY_F2: switch_fg_pcb(PCB_BIRD); return 0;
      case AM_KEY_F3: switch_fg_pcb(PCB_NSLIDER); return 0;
    }
  }

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
  AM_GPU_CONFIG_T gpu = io_read(AM_GPU_CONFIG);

  if (len == 0) {
    io_write(AM_GPU_FBDRAW, 0, 0, NULL, 0, 0, true);
    return 0;
  }

  size_t pixel_offset = offset / sizeof(uint32_t);
  int x = pixel_offset % gpu.width;
  int y = pixel_offset / gpu.width;
  size_t remaining = len / sizeof(uint32_t);
  const uint32_t *pixels = (const uint32_t *)buf;

  while (remaining > 0) {
    int row_w = gpu.width - x;
    if ((size_t)row_w > remaining) row_w = remaining;
    io_write(AM_GPU_FBDRAW, x, y, (void *)pixels, row_w, 1, false);
    remaining -= row_w;
    pixels += row_w;
    y ++;
    x = 0;
  }
  return len;
}

size_t sbctl_read(void *buf, size_t offset, size_t len) {
  (void)offset;
  if (len < sizeof(int)) return 0;

  AM_AUDIO_CONFIG_T cfg = io_read(AM_AUDIO_CONFIG);
  AM_AUDIO_STATUS_T stat = io_read(AM_AUDIO_STATUS);
  int free_bytes = cfg.bufsize - stat.count;
  if (free_bytes < 0) free_bytes = 0;

  memcpy(buf, &free_bytes, sizeof(free_bytes));
  return sizeof(free_bytes);
}

size_t sbctl_write(const void *buf, size_t offset, size_t len) {
  (void)offset;
  if (len < sizeof(int) * 3) return 0;

  const int *args = (const int *)buf;
  io_write(AM_AUDIO_CTRL, args[0], args[1], args[2]);
  return sizeof(int) * 3;
}

size_t sb_write(const void *buf, size_t offset, size_t len) {
  (void)offset;
  if (len == 0) return 0;
  io_write(AM_AUDIO_PLAY, (Area){ .start = (void *)buf, .end = (void *)buf + len });
  return len;
}

void init_device() {
  Log("Initializing devices...");
  ioe_init();
}
