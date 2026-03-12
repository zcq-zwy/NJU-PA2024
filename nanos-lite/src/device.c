#include <common.h>
#include <proc.h>

#define NAME(key)   [AM_KEY_##key] = #key,

static const char *keyname[256] __attribute__((used)) = {
  [AM_KEY_NONE] = "NONE",
  AM_KEYS(NAME)
};

static int audio_freq = 0;
static int audio_channels = 0;
static int audio_samples = 0;

size_t mm_used_bytes(void);
size_t mm_total_bytes(void);
size_t fs_storage_used_bytes(void);
size_t fs_storage_total_bytes(void);
void proc_sched_stats(uint64_t *fg_runs, uint64_t *bg_runs, uint64_t *total_runs);

void reset_audio_on_switch(void) {
  if (audio_freq > 0 && audio_channels > 0 && audio_samples > 0) {
    io_write(AM_AUDIO_CTRL, audio_freq, audio_channels, audio_samples);
  }
}

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
      case AM_KEY_F3: switch_fg_pcb(PCB_ONSCRIPTER); return 0;
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

size_t sysinfo_read(void *buf, size_t offset, size_t len) {
  if (len == 0) return 0;

  size_t mem_used = mm_used_bytes();
  size_t mem_total = mm_total_bytes();
  size_t storage_used = fs_storage_used_bytes();
  size_t storage_total = fs_storage_total_bytes();
  uint64_t fg_runs = 0, bg_runs = 0, total_runs = 0;
  proc_sched_stats(&fg_runs, &bg_runs, &total_runs);
  int fg_share = 0, bg_share = 0;
  if (total_runs != 0) {
    fg_share = (int)(fg_runs * 100 / total_runs);
    bg_share = (int)(bg_runs * 100 / total_runs);
  }
  AM_TIMER_UPTIME_T uptime = io_read(AM_TIMER_UPTIME);

  char info[256];
  int n = snprintf(info, sizeof(info),
      "uptime_ms: %llu\n"
      "memory: %u / %u KB (%u%%)\n"
      "storage: %u / %u KB (%u%%)\n"
      "cpu: busy 100%%, fg %d%%, bg %d%%\n",
      (unsigned long long)(uptime.us / 1000),
      (unsigned)(mem_used / 1024), (unsigned)(mem_total / 1024),
      mem_total ? (unsigned)(mem_used * 100 / mem_total) : 0,
      (unsigned)(storage_used / 1024), (unsigned)(storage_total / 1024),
      storage_total ? (unsigned)(storage_used * 100 / storage_total) : 0,
      fg_share, bg_share);
  if (n <= 0) return 0;
  if ((size_t)offset >= (size_t)n) return 0;

  size_t copy_len = n - offset;
  if (copy_len > len) copy_len = len;
  memcpy(buf, info + offset, copy_len);
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
  audio_freq = args[0];
  audio_channels = args[1];
  audio_samples = args[2];
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
