#include <NDL.h>
#include <SDL.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_AudioSpec audio_spec = {};
static uint8_t *audio_buf = NULL;
static int audio_opened = 0;
static int audio_paused = 1;
static uint32_t callback_interval = 0;
static uint32_t last_callback = 0;
static int in_callback = 0;
static int audio_locked = 0;

static int bytes_per_sample(uint16_t format) {
  switch (format) {
    case AUDIO_U8: return 1;
    case AUDIO_S16: return 2;
    default: assert(0); return 0;
  }
}

static uint16_t read_le16(const uint8_t *buf) {
  return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_le32(const uint8_t *buf) {
  return (uint32_t)buf[0]
    | ((uint32_t)buf[1] << 8)
    | ((uint32_t)buf[2] << 16)
    | ((uint32_t)buf[3] << 24);
}

void __SDL_AudioCallbackHelper(void) {
  if (!audio_opened || audio_paused || audio_spec.callback == NULL || in_callback || audio_locked > 0) {
    return;
  }

  uint32_t now = SDL_GetTicks();
  if (now - last_callback < callback_interval) {
    return;
  }

  int align = bytes_per_sample(audio_spec.format) * audio_spec.channels;
  if (align <= 0) {
    return;
  }

  while (now - last_callback >= callback_interval) {
    int free_bytes = NDL_QueryAudio();
    if (free_bytes <= 0) {
      break;
    }

    int len = audio_spec.size;
    if (len > free_bytes) {
      len = free_bytes;
    }
    len = len / align * align;
    if (len <= 0) {
      break;
    }

    memset(audio_buf, 0, len);
    in_callback = 1;
    audio_spec.callback(audio_spec.userdata, audio_buf, len);
    in_callback = 0;
    NDL_PlayAudio(audio_buf, len);
    last_callback += callback_interval;
    now = SDL_GetTicks();
  }
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
  assert(desired != NULL);
  assert(desired->channels > 0);
  assert(desired->samples > 0);

  if (audio_opened) {
    SDL_CloseAudio();
  }

  audio_spec = *desired;
  audio_spec.size = desired->samples * desired->channels * bytes_per_sample(desired->format);
  callback_interval = desired->samples * 1000 / desired->freq;
  if (callback_interval == 0) {
    callback_interval = 1;
  }

  audio_buf = malloc(audio_spec.size);
  assert(audio_buf != NULL);
  memset(audio_buf, 0, audio_spec.size);

  NDL_OpenAudio(audio_spec.freq, audio_spec.channels, audio_spec.samples);
  audio_opened = 1;
  audio_paused = 1;
  last_callback = SDL_GetTicks();

  if (obtained != NULL) {
    *obtained = audio_spec;
  }
  return 0;
}

void SDL_CloseAudio() {
  if (!audio_opened) {
    return;
  }
  NDL_CloseAudio();
  free(audio_buf);
  audio_buf = NULL;
  audio_opened = 0;
  audio_paused = 1;
  callback_interval = 0;
  last_callback = 0;
  in_callback = 0;
  audio_locked = 0;
  memset(&audio_spec, 0, sizeof(audio_spec));
}

void SDL_PauseAudio(int pause_on) {
  audio_paused = pause_on;
  if (!audio_paused) {
    last_callback = SDL_GetTicks();
  }
}

void SDL_MixAudio(uint8_t *dst, uint8_t *src, uint32_t len, int volume) {
  if (dst == NULL || src == NULL || len == 0 || volume <= 0) {
    return;
  }
  if (volume > SDL_MIX_MAXVOLUME) {
    volume = SDL_MIX_MAXVOLUME;
  }

  switch (audio_spec.format) {
    case AUDIO_U8: {
      for (uint32_t i = 0; i < len; i++) {
        int sample = ((int)src[i] - 128) * volume / SDL_MIX_MAXVOLUME;
        int mixed = (int)dst[i] + sample;
        if (mixed < 0) mixed = 0;
        if (mixed > 255) mixed = 255;
        dst[i] = (uint8_t)mixed;
      }
      break;
    }
    case AUDIO_S16: {
      int16_t *d = (int16_t *)dst;
      int16_t *s = (int16_t *)src;
      uint32_t samples = len / sizeof(int16_t);
      for (uint32_t i = 0; i < samples; i++) {
        int mixed = (int)d[i] + (int)s[i] * volume / SDL_MIX_MAXVOLUME;
        if (mixed < -32768) mixed = -32768;
        if (mixed > 32767) mixed = 32767;
        d[i] = (int16_t)mixed;
      }
      break;
    }
    default:
      assert(0);
  }
}

SDL_AudioSpec *SDL_LoadWAV(const char *file, SDL_AudioSpec *spec, uint8_t **out_audio_buf, uint32_t *out_audio_len) {
  assert(file != NULL);
  assert(spec != NULL);
  assert(out_audio_buf != NULL);
  assert(out_audio_len != NULL);

  FILE *fp = fopen(file, "rb");
  if (fp == NULL) {
    return NULL;
  }

  uint8_t riff[12];
  if (fread(riff, 1, sizeof(riff), fp) != sizeof(riff)) {
    fclose(fp);
    return NULL;
  }
  if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    fclose(fp);
    return NULL;
  }

  int got_fmt = 0;
  int got_data = 0;
  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint32_t sample_rate = 0;
  uint16_t bits_per_sample = 0;
  uint8_t *data = NULL;
  uint32_t data_size = 0;

  while (!got_fmt || !got_data) {
    uint8_t chunk_hdr[8];
    if (fread(chunk_hdr, 1, sizeof(chunk_hdr), fp) != sizeof(chunk_hdr)) {
      break;
    }

    uint32_t chunk_size = read_le32(chunk_hdr + 4);
    if (memcmp(chunk_hdr, "fmt ", 4) == 0) {
      uint8_t *buf = malloc(chunk_size);
      assert(buf != NULL);
      if (fread(buf, 1, chunk_size, fp) != chunk_size) {
        free(buf);
        break;
      }
      audio_format = read_le16(buf + 0);
      channels = read_le16(buf + 2);
      sample_rate = read_le32(buf + 4);
      bits_per_sample = read_le16(buf + 14);
      free(buf);
      got_fmt = 1;
    } else if (memcmp(chunk_hdr, "data", 4) == 0) {
      data = malloc(chunk_size);
      assert(data != NULL);
      if (fread(data, 1, chunk_size, fp) != chunk_size) {
        free(data);
        data = NULL;
        break;
      }
      data_size = chunk_size;
      got_data = 1;
    } else {
      fseek(fp, chunk_size, SEEK_CUR);
    }

    if (chunk_size & 1) {
      fseek(fp, 1, SEEK_CUR);
    }
  }

  fclose(fp);

  if (!got_fmt || !got_data || audio_format != 1) {
    free(data);
    return NULL;
  }

  spec->freq = sample_rate;
  spec->channels = channels;
  spec->format = (bits_per_sample == 8 ? AUDIO_U8 : AUDIO_S16);
  spec->samples = 4096;
  spec->size = data_size;
  spec->callback = NULL;
  spec->userdata = NULL;

  *out_audio_buf = data;
  *out_audio_len = data_size;
  return spec;
}

void SDL_FreeWAV(uint8_t *wav_audio_buf) {
  free(wav_audio_buf);
}

void SDL_LockAudio() {
  audio_locked++;
}

void SDL_UnlockAudio() {
  if (audio_locked > 0) {
    audio_locked--;
  }
}
