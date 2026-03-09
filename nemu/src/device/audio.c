#include <common.h>
#include <device/map.h>
#include <device/snapshot.h>
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

static bool audio_need_prefill = true;
static uint32_t audio_prefill_bytes = 0;

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
static void audio_stop() {
  if (!audio_started) return;

  SDL_CloseAudio();
  audio_started = false;
  audio_base[reg_init] = 0;
}


static void audio_callback(void *userdata, uint8_t *stream, int len) {
  (void)userdata;

  if (len <= 0) return;

  uint32_t req = (uint32_t)len;

  // Recover from underrun by waiting until enough data is queued.
  if (audio_need_prefill) {
    if (sbuf_count < audio_prefill_bytes) {
      memset(stream, 0, req);
      audio_base[reg_count] = sbuf_count;
      return;
    }
    audio_need_prefill = false;
  }

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
    audio_need_prefill = true;
    memset(stream + nread, 0, miss);

    uint32_t now = SDL_GetTicks();
    if (now - last_underflow_report_ms >= 5000) {
      last_underflow_report_ms = now;
      Log("audio underflow: events=%" PRIu64 ", bytes=%" PRIu64 ", sbuf_count=%u, req=%d, got=%d, prefill=%u",
          underflow_events, underflow_bytes, sbuf_count, len, (int)nread, audio_prefill_bytes);
    }
  }

  sbuf_count -= nread;
  audio_base[reg_count] = sbuf_count;
}

static void audio_start() {
  if (audio_started) {
    audio_stop();
  }

  SDL_AudioSpec want = {};
  want.freq = audio_base[reg_freq];
  want.format = AUDIO_S16SYS;
  want.channels = audio_base[reg_channels];
  want.samples = audio_base[reg_samples];
  if (want.samples < 2048) want.samples = 2048;
  want.callback = audio_callback;
  want.userdata = NULL;

  if (want.freq == 0 || want.channels == 0 || want.samples == 0) {
    audio_base[reg_init] = 0;
    return;
  }

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    Log("audio: SDL_InitSubSystem failed: %s", SDL_GetError());
    audio_base[reg_init] = 0;
    return;
  }

  SDL_AudioSpec have = {};
  if (SDL_OpenAudio(&want, &have) != 0) {
    Log("audio: SDL_OpenAudio failed: %s", SDL_GetError());
    audio_base[reg_init] = 0;
    return;
  }

  if (have.format != AUDIO_S16SYS) {
    Log("audio: host format 0x%x != AUDIO_S16SYS(0x%x), may cause distortion", have.format, AUDIO_S16SYS);
  }
  if (have.channels != want.channels || have.freq != want.freq) {
    Log("audio: host spec differs (want %uHz/%uch, have %dHz/%uch)",
        want.freq, want.channels, have.freq, have.channels);
  }

  uint32_t cap = audio_base[reg_sbuf_size];
  uint32_t period_bytes = have.size;
  if (period_bytes == 0) {
    period_bytes = have.samples * have.channels * (uint32_t)sizeof(int16_t);
  }
  if (period_bytes == 0) period_bytes = 4096;

  uint64_t target = (uint64_t)period_bytes * 4;
  if (target < 8192) target = 8192;
  uint32_t cap_half = cap / 2;
  if (cap_half > 0 && target > cap_half) target = cap_half;
  if (target > cap) target = cap;
  if (target == 0) target = period_bytes;

  audio_prefill_bytes = (uint32_t)target;
  audio_need_prefill = true;

  SDL_PauseAudio(0);
  audio_started = true;
  audio_base[reg_init] = 1;
  underflow_events = 0;
  underflow_bytes = 0;
  last_underflow_report_ms = SDL_GetTicks();
  Log("audio: started (want=%uHz/%uch/%usmp, have=%dHz/%uch/%usmp, prefill=%u)",
      want.freq, want.channels, want.samples,
      have.freq, have.channels, have.samples,
      audio_prefill_bytes);
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
        audio_need_prefill = true;
        audio_base[reg_count] = 0;
        audio_unlock_if_started();
        audio_start();
      }
      break;

    case reg_count:
      if (!audio_started) {
        audio_base[reg_count] = 0;
        break;
      }

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
#else
  add_mmio_map("audio", CONFIG_AUDIO_CTL_MMIO, audio_base, space_size, audio_io_handler);
#endif

  // Audio stream payload is always mapped at MMIO AUDIO_SBUF_ADDR.
  add_mmio_map("audio-sbuf", CONFIG_SB_ADDR, sbuf, CONFIG_SB_SIZE, NULL);
}

void audio_snapshot_save(AudioSnapshot *out) {
  memset(out, 0, sizeof(*out));
  audio_lock_if_started();
  out->sbuf_rpos = sbuf_rpos;
  out->sbuf_count = sbuf_count;
  out->audio_started = audio_started;
  out->audio_need_prefill = audio_need_prefill;
  out->audio_prefill_bytes = audio_prefill_bytes;
  out->underflow_events = underflow_events;
  out->underflow_bytes = underflow_bytes;
  out->last_underflow_report_ms = last_underflow_report_ms;
  audio_unlock_if_started();
}

void audio_snapshot_load(const AudioSnapshot *in) {
  bool was_started = audio_started;
  if (was_started) SDL_LockAudio();

  if (was_started && !in->audio_started) {
    audio_stop();
    was_started = false;
  }

  if (!was_started && in->audio_started) {
    audio_start();
    if (audio_started) SDL_LockAudio();
  }

  sbuf_rpos = in->sbuf_rpos;
  sbuf_count = in->sbuf_count;
  audio_need_prefill = in->audio_need_prefill;
  audio_prefill_bytes = in->audio_prefill_bytes;
  underflow_events = in->underflow_events;
  underflow_bytes = in->underflow_bytes;
  last_underflow_report_ms = in->last_underflow_report_ms;
  audio_base[reg_count] = sbuf_count;
  audio_base[reg_init] = audio_started ? 1 : 0;

  if (audio_started) SDL_UnlockAudio();
}
