#include <am.h>
#include <stdio.h>
#include <klib-macros.h>

#define FPS 30
#define FRAME_US (1000000 / FPS)
#define AUDIO_BYTES_PER_SEC (AUDIO_FREQ * AUDIO_CHANNEL * (int)sizeof(int16_t))
#define AUDIO_FRAME_BYTES (AUDIO_CHANNEL * (int)sizeof(int16_t))
#define AUDIO_FEED_CHUNK 4096

#define CHAR_WHITE '.'
#define CHAR_BLACK 'X'

typedef struct {
  uint8_t pixel[VIDEO_ROW * VIDEO_COL / 8];
} frame_t;

static void sleep_until(uint64_t next) {
  while (io_read(AM_TIMER_UPTIME).us < next) ;
}

static uint8_t getbit(uint8_t *p, int idx) {
  int byte_idx = idx / 8;
  int bit_idx = idx % 8;
  bit_idx = 7 - bit_idx;
  uint8_t byte = p[byte_idx];
  uint8_t bit = (byte >> bit_idx) & 1;
  return bit;
}

static void play_audio_bytes(uint8_t **cur, int *left, int nbytes) {
  Area sbuf;
  while (nbytes > 0 && *left > 0) {
    int len = nbytes > AUDIO_FEED_CHUNK ? AUDIO_FEED_CHUNK : nbytes;
    if (len > *left) len = *left;
    len = (len / AUDIO_FRAME_BYTES) * AUDIO_FRAME_BYTES;
    if (len <= 0) break;

    sbuf.start = *cur;
    sbuf.end = *cur + len;
    io_write(AM_AUDIO_PLAY, sbuf);

    *cur += len;
    *left -= len;
    nbytes -= len;
  }
}

static void refill_audio_to_target(uint8_t **cur, int *left, int target_level) {
  if (*left <= 0 || target_level <= 0) return;

  int count = io_read(AM_AUDIO_STATUS).count;
  int need = target_level - count;
  if (need <= 0) return;
  if (need > *left) need = *left;

  need = (need / AUDIO_FRAME_BYTES) * AUDIO_FRAME_BYTES;
  if (need <= 0) return;

  play_audio_bytes(cur, left, need);
}

int main() {
  extern uint8_t video_payload, video_payload_end;
  extern uint8_t audio_payload, audio_payload_end;

  frame_t *f = (void *)&video_payload;
  frame_t *fend = (void *)&video_payload_end;

  uint8_t *audio_cur = &audio_payload;
  int audio_left = &audio_payload_end - &audio_payload;
  bool has_audio;
  int audio_target = 0;

  ioe_init();
  printf("\033[H\033[J");  // screen_clear

  AM_AUDIO_CONFIG_T acfg = io_read(AM_AUDIO_CONFIG);
  has_audio = acfg.present;
  if (has_audio) {
    io_write(AM_AUDIO_CTRL, AUDIO_FREQ, AUDIO_CHANNEL, 1024);

    int half_buf = acfg.bufsize / 2;
    int quarter_sec = AUDIO_BYTES_PER_SEC / 4;
    audio_target = half_buf;
    if (audio_target < quarter_sec) audio_target = quarter_sec;

    int max_target = acfg.bufsize - AUDIO_FEED_CHUNK;
    if (audio_target > max_target) audio_target = max_target;
    if (audio_target < AUDIO_FRAME_BYTES) audio_target = AUDIO_FRAME_BYTES;

    refill_audio_to_target(&audio_cur, &audio_left, audio_target);
  }

  uint64_t frame_deadline = io_read(AM_TIMER_UPTIME).us;
  for (; f < fend; f++) {
    if (has_audio && audio_left > 0) {
      refill_audio_to_target(&audio_cur, &audio_left, audio_target);
    }

    printf("\033[0;0H");  // reset cursor
    for (int y = 0; y < VIDEO_ROW; y++) {
      for (int x = 0; x < VIDEO_COL; x++) {
        uint8_t p = getbit(f->pixel, y * VIDEO_COL + x);
        putch(p ? CHAR_BLACK : CHAR_WHITE);
      }
      putch('\n');
    }

    frame_deadline += FRAME_US;
    uint64_t now_us = io_read(AM_TIMER_UPTIME).us;
    if (now_us < frame_deadline) {
      sleep_until(frame_deadline);
    } else {
      frame_deadline = now_us;
    }
  }

  return 0;
}
