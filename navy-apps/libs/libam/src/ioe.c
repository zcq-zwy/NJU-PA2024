#include <am.h>
#include <NDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool initialized = false;
static int screen_w = 0;
static int screen_h = 0;

#define KEYNAME(k) #k,
static const char *keynames[] = {
  "NONE",
  AM_KEYS(KEYNAME)
};

static int lookup_key(const char *name) {
  for (int i = 0; i < (int)(sizeof(keynames) / sizeof(keynames[0])); i++) {
    if (strcmp(keynames[i], name) == 0) {
      return i;
    }
  }
  return AM_KEY_NONE;
}

static void read_dispinfo(void) {
  if (screen_w != 0 && screen_h != 0) {
    return;
  }
  int w = 0, h = 0;
  NDL_OpenCanvas(&w, &h);
  screen_w = w;
  screen_h = h;
}

static void __am_timer_config(AM_TIMER_CONFIG_T *cfg) {
  cfg->present = true;
  cfg->has_rtc = true;
}

static void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  rtc->year = tm_info->tm_year + 1900;
  rtc->month = tm_info->tm_mon + 1;
  rtc->day = tm_info->tm_mday;
  rtc->hour = tm_info->tm_hour;
  rtc->minute = tm_info->tm_min;
  rtc->second = tm_info->tm_sec;
}

static void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  uptime->us = (uint64_t)NDL_GetTicks() * 1000;
}

static void __am_input_config(AM_INPUT_CONFIG_T *cfg) {
  cfg->present = true;
}

static void __am_input_keybrd(AM_INPUT_KEYBRD_T *kbd) {
  char buf[64] = {};
  kbd->keydown = false;
  kbd->keycode = AM_KEY_NONE;

  if (NDL_PollEvent(buf, sizeof(buf)) == 0) {
    return;
  }

  char type[3] = {};
  char name[32] = {};
  if (sscanf(buf, "%2s %31s", type, name) != 2) {
    return;
  }

  kbd->keycode = lookup_key(name);
  if (kbd->keycode == AM_KEY_NONE) {
    return;
  }

  if (strcmp(type, "kd") == 0) {
    kbd->keydown = true;
  } else if (strcmp(type, "ku") == 0) {
    kbd->keydown = false;
  } else {
    kbd->keycode = AM_KEY_NONE;
  }
}

static void __am_gpu_config(AM_GPU_CONFIG_T *cfg) {
  read_dispinfo();
  cfg->present = true;
  cfg->has_accel = false;
  cfg->width = screen_w;
  cfg->height = screen_h;
  cfg->vmemsz = screen_w * screen_h * (int)sizeof(uint32_t);
}

static void __am_gpu_status(AM_GPU_STATUS_T *status) {
  status->ready = true;
}

static void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *ctl) {
  if (ctl->pixels != NULL && ctl->w > 0 && ctl->h > 0) {
    NDL_DrawRect((uint32_t *)ctl->pixels, ctl->x, ctl->y, ctl->w, ctl->h);
  }
}

static void __am_uart_config(AM_UART_CONFIG_T *cfg) {
  cfg->present = true;
}

static void __am_uart_tx(AM_UART_TX_T *tx) {
  putch(tx->data);
}

static void __am_uart_rx(AM_UART_RX_T *rx) {
  rx->data = '\0';
}

static void __am_audio_config(AM_AUDIO_CONFIG_T *cfg) {
  cfg->present = false;
  cfg->bufsize = 0;
}

static void __am_audio_ctrl(AM_AUDIO_CTRL_T *ctrl) {
  (void)ctrl;
}

static void __am_audio_status(AM_AUDIO_STATUS_T *status) {
  status->count = 0;
}

static void __am_audio_play(AM_AUDIO_PLAY_T *play) {
  (void)play;
}

static void __am_disk_config(AM_DISK_CONFIG_T *cfg) {
  cfg->present = false;
  cfg->blksz = 0;
  cfg->blkcnt = 0;
}

static void __am_disk_status(AM_DISK_STATUS_T *status) {
  status->ready = false;
}

static void __am_disk_blkio(AM_DISK_BLKIO_T *io) {
  (void)io;
}

static void __am_net_config(AM_NET_CONFIG_T *cfg) {
  cfg->present = false;
}

typedef void (*handler_t)(void *);
static handler_t lut[128] = {
  [AM_TIMER_CONFIG] = (handler_t)__am_timer_config,
  [AM_TIMER_RTC]    = (handler_t)__am_timer_rtc,
  [AM_TIMER_UPTIME] = (handler_t)__am_timer_uptime,
  [AM_INPUT_CONFIG] = (handler_t)__am_input_config,
  [AM_INPUT_KEYBRD] = (handler_t)__am_input_keybrd,
  [AM_GPU_CONFIG]   = (handler_t)__am_gpu_config,
  [AM_GPU_STATUS]   = (handler_t)__am_gpu_status,
  [AM_GPU_FBDRAW]   = (handler_t)__am_gpu_fbdraw,
  [AM_UART_CONFIG]  = (handler_t)__am_uart_config,
  [AM_UART_TX]      = (handler_t)__am_uart_tx,
  [AM_UART_RX]      = (handler_t)__am_uart_rx,
  [AM_AUDIO_CONFIG] = (handler_t)__am_audio_config,
  [AM_AUDIO_CTRL]   = (handler_t)__am_audio_ctrl,
  [AM_AUDIO_STATUS] = (handler_t)__am_audio_status,
  [AM_AUDIO_PLAY]   = (handler_t)__am_audio_play,
  [AM_DISK_CONFIG]  = (handler_t)__am_disk_config,
  [AM_DISK_STATUS]  = (handler_t)__am_disk_status,
  [AM_DISK_BLKIO]   = (handler_t)__am_disk_blkio,
  [AM_NET_CONFIG]   = (handler_t)__am_net_config,
};

bool ioe_init() {
  if (!initialized) {
    NDL_Init(0);
    read_dispinfo();
    initialized = true;
  }
  return true;
}

static void do_io(int reg, void *buf) {
  if (!initialized) {
    ioe_init();
  }
  if (reg < 0 || reg >= (int)(sizeof(lut) / sizeof(lut[0])) || lut[reg] == NULL) {
    return;
  }
  lut[reg](buf);
}

void ioe_read(int reg, void *buf) {
  do_io(reg, buf);
}

void ioe_write(int reg, void *buf) {
  do_io(reg, buf);
}
