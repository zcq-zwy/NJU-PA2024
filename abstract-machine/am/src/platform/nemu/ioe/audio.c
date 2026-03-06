#include <am.h>
#include <nemu.h>
#include <klib.h>

#define AUDIO_FREQ_ADDR      (AUDIO_ADDR + 0x00)
#define AUDIO_CHANNELS_ADDR  (AUDIO_ADDR + 0x04)
#define AUDIO_SAMPLES_ADDR   (AUDIO_ADDR + 0x08)
#define AUDIO_SBUF_SIZE_ADDR (AUDIO_ADDR + 0x0c)
#define AUDIO_INIT_ADDR      (AUDIO_ADDR + 0x10)
#define AUDIO_COUNT_ADDR     (AUDIO_ADDR + 0x14)

static int sbuf_size = 0;
static int sbuf_wpos = 0;

void __am_audio_init() {
  sbuf_size = inl(AUDIO_SBUF_SIZE_ADDR);
  sbuf_wpos = 0;
}

void __am_audio_config(AM_AUDIO_CONFIG_T *cfg) {
  if (sbuf_size == 0) {
    sbuf_size = inl(AUDIO_SBUF_SIZE_ADDR);
  }
  cfg->present = true;
  cfg->bufsize = sbuf_size;
}

void __am_audio_ctrl(AM_AUDIO_CTRL_T *ctrl) {
  outl(AUDIO_FREQ_ADDR, ctrl->freq);
  outl(AUDIO_CHANNELS_ADDR, ctrl->channels);
  outl(AUDIO_SAMPLES_ADDR, ctrl->samples);
  outl(AUDIO_INIT_ADDR, 1);
}

void __am_audio_status(AM_AUDIO_STATUS_T *stat) {
  stat->count = inl(AUDIO_COUNT_ADDR);
}

void __am_audio_play(AM_AUDIO_PLAY_T *ctl) {
  uint8_t *src = (uint8_t *)ctl->buf.start;
  int left = ctl->buf.end - ctl->buf.start;

  const int chunk_limit = 4096;

  while (left > 0) {
    int count = inl(AUDIO_COUNT_ADDR);
    int free_space = sbuf_size - count;
    if (free_space <= 0) continue;

    int nwrite = left;
    if (nwrite > free_space) nwrite = free_space;
    if (nwrite > chunk_limit) nwrite = chunk_limit;

    int till_end = sbuf_size - sbuf_wpos;
    int first = nwrite;
    if (first > till_end) first = till_end;

    memcpy((void *)(uintptr_t)(AUDIO_SBUF_ADDR + sbuf_wpos), src, first);
    if (nwrite > first) {
      memcpy((void *)(uintptr_t)AUDIO_SBUF_ADDR, src + first, nwrite - first);
    }

#if defined(__riscv)
    // Keep MMIO notify ordered after payload stores.
    asm volatile("fence w, w" ::: "memory");
#else
    asm volatile("" ::: "memory");
#endif

    sbuf_wpos = (sbuf_wpos + nwrite) % sbuf_size;
    src += nwrite;
    left -= nwrite;

    outl(AUDIO_COUNT_ADDR, nwrite);
  }
}
