#include <common.h>
#include <device/map.h>
#include <SDL2/SDL.h>

enum {
  reg_freq,
  reg_channels,
  reg_samples,
  reg_sbuf_size,
  reg_init,
  reg_count,
  nr_reg
};

static uint8_t *sbuf = NULL;
static uint32_t *audio_base = NULL;

static uint32_t sbuf_rpos = 0;
static uint32_t sbuf_count = 0;
static bool audio_started = false;

static uint64_t underflow_events = 0;
static uint64_t underflow_bytes = 0;
static uint32_t last_underflow_report_ms = 0;

static inline void audio_lock_if_started(void) {
  if (audio_started) SDL_LockAudio();
}

static inline void audio_unlock_if_started(void) {
  if (audio_started) SDL_UnlockAudio();
}

static inline uint32_t u32_min(uint32_t a, uint32_t b) {
  return a < b ? a : b;
}

static void audio_callback(void *userdata, uint8_t *stream, int len) {
  (void)userdata;

  if (len <= 0) return;

  uint32_t req = (uint32_t)len;
  uint32_t nread = u32_min(sbuf_count, req);
  uint32_t sbuf_size = audio_base[reg_sbuf_size];

  if (nread > 0) {
    uint32_t first = u32_min(nread, sbuf_size - sbuf_rpos);
    memcpy(stream, sbuf + sbuf_rpos, first);
    if (nread > first) {
      memcpy(stream + first, sbuf, nread - first);
    }
    sbuf_rpos = (sbuf_rpos + nread) % sbuf_size;
  }

  if (req > nread) {
    uint32_t miss = req - nread;
    underflow_events++;
    underflow_bytes += miss;
    memset(stream + nread, 0, miss);

    uint32_t now = SDL_GetTicks();
    if (now - last_underflow_report_ms >= 5000) {
      last_underflow_report_ms = now;
      Log("audio underflow: events=%" PRIu64 ", bytes=%" PRIu64 ", sbuf_count=%u, req=%d, got=%d",
          underflow_events, underflow_bytes, sbuf_count, len, (int)nread);
    }
  }

  sbuf_count -= nread;
  audio_base[reg_count] = sbuf_count;
}

static void audio_start() {
  if (audio_started) return;

  SDL_AudioSpec s = {};
  s.freq = audio_base[reg_freq];
  s.format = AUDIO_S16SYS;
  s.channels = audio_base[reg_channels];
  s.samples = audio_base[reg_samples];
  s.callback = audio_callback;
  s.userdata = NULL;

  if (s.freq == 0 || s.channels == 0 || s.samples == 0) {
    return;
  }

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    Log("audio: SDL_InitSubSystem failed: %s", SDL_GetError());
    return;
  }

  if (SDL_OpenAudio(&s, NULL) != 0) {
    Log("audio: SDL_OpenAudio failed: %s", SDL_GetError());
    return;
  }

  SDL_PauseAudio(0);
  audio_started = true;
  underflow_events = 0;
  underflow_bytes = 0;
  last_underflow_report_ms = SDL_GetTicks();
  Log("audio: started (freq=%u, channels=%u, samples=%u)",
      audio_base[reg_freq], audio_base[reg_channels], audio_base[reg_samples]);
}

static void audio_io_handler(uint32_t offset, int len, bool is_write) {
  assert(len == 4);
  int reg = offset / sizeof(uint32_t);
  assert(reg >= 0 && reg < nr_reg);

  switch (reg) {
    case reg_init:
      if (is_write && audio_base[reg_init]) {
        audio_lock_if_started();
        sbuf_rpos = 0;
        sbuf_count = 0;
        audio_base[reg_count] = 0;
        audio_unlock_if_started();
        audio_start();
      }
      break;

    case reg_count:
      audio_lock_if_started();
      if (is_write) {
        uint32_t req = audio_base[reg_count];
        uint32_t cap = audio_base[reg_sbuf_size];
        uint32_t can = cap - sbuf_count;
        if (req > can) req = can;
        sbuf_count += req;
        audio_base[reg_count] = sbuf_count;
      } else {
        audio_base[reg_count] = sbuf_count;
      }
      audio_unlock_if_started();
      break;

    default:
      break;
  }
}

void init_audio() {
  uint32_t space_size = sizeof(uint32_t) * nr_reg;
  audio_base = (uint32_t *)new_space(space_size);
  memset(audio_base, 0, space_size);
  audio_base[reg_sbuf_size] = CONFIG_SB_SIZE;

  sbuf = (uint8_t *)new_space(CONFIG_SB_SIZE);
  memset(sbuf, 0, CONFIG_SB_SIZE);

#ifdef CONFIG_HAS_PORT_IO
  add_pio_map("audio", CONFIG_AUDIO_CTL_PORT, audio_base, space_size, audio_io_handler);
  add_pio_map("audio-sbuf", CONFIG_SB_ADDR, sbuf, CONFIG_SB_SIZE, NULL);
#else
  add_mmio_map("audio", CONFIG_AUDIO_CTL_MMIO, audio_base, space_size, audio_io_handler);
  add_mmio_map("audio-sbuf", CONFIG_SB_ADDR, sbuf, CONFIG_SB_SIZE, NULL);
#endif
}
