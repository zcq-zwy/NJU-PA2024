#include <SDL_mixer.h>
#include <vorbis.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Mix_Music {
  uint8_t *buf;
  uint32_t len;
  uint32_t pos;
  int loops;
};

struct Mix_Chunk {
  uint8_t *buf;
  uint32_t len;
};

typedef struct {
  Mix_Chunk *chunk;
  uint32_t pos;
  int loops;
  int volume;
  int playing;
} Mix_ChannelState;

static SDL_AudioSpec mixer_spec = {};
static int mixer_opened = 0;
static int allocated_channels = 0;
static int music_playing = 0;
static int music_volume = MIX_MAX_VOLUME;
static Mix_Music *current_music = NULL;
static Mix_ChannelState *channels = NULL;
static void (*channel_finished_cb)(int channel) = NULL;
static void (*music_finished_cb)(void) = NULL;
static char mixer_error[128] = "";

static void set_error(const char *msg) {
  snprintf(mixer_error, sizeof(mixer_error), "%s", msg == NULL ? "" : msg);
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

static int read_rwops_all(SDL_RWops *src, uint8_t **out_buf, int *out_len) {
  int64_t size = SDL_RWsize(src);
  if (size <= 0) {
    set_error("invalid rwops size");
    return -1;
  }

  uint8_t *buf = (uint8_t *)malloc(size);
  assert(buf != NULL);
  SDL_RWseek(src, 0, RW_SEEK_SET);
  size_t nread = SDL_RWread(src, buf, size, 1);
  if (nread != 1) {
    free(buf);
    set_error("SDL_RWread failed");
    return -1;
  }

  *out_buf = buf;
  *out_len = size;
  return 0;
}

static void scale_s16_samples(int16_t *samples, int count, int volume) {
  if (volume >= MIX_MAX_VOLUME) return;
  if (volume <= 0) {
    memset(samples, 0, count * sizeof(*samples));
    return;
  }
  for (int i = 0; i < count; i++) {
    samples[i] = samples[i] * volume / MIX_MAX_VOLUME;
  }
}

static int convert_pcm_s16(
    const int16_t *src, int src_frames, int src_rate, int src_channels,
    uint8_t **out_buf, uint32_t *out_len) {
  if (!mixer_opened) {
    set_error("audio device is not opened");
    return -1;
  }
  if (mixer_spec.format != AUDIO_S16SYS) {
    set_error("only AUDIO_S16SYS is supported");
    return -1;
  }
  if (src == NULL || src_frames <= 0 || src_rate <= 0 || src_channels <= 0) {
    set_error("invalid pcm source");
    return -1;
  }

  int dst_rate = mixer_spec.freq;
  int dst_channels = mixer_spec.channels;
  int dst_frames = (int)(((int64_t)src_frames * dst_rate + src_rate - 1) / src_rate);
  if (dst_frames <= 0) dst_frames = src_frames;

  uint32_t total_samples = (uint32_t)dst_frames * dst_channels;
  int16_t *dst = (int16_t *)malloc(total_samples * sizeof(int16_t));
  assert(dst != NULL);

  for (int dst_frame = 0; dst_frame < dst_frames; dst_frame++) {
    int src_frame = (int)((int64_t)dst_frame * src_rate / dst_rate);
    if (src_frame >= src_frames) src_frame = src_frames - 1;
    const int16_t *src_frame_ptr = src + src_frame * src_channels;

    for (int dst_ch = 0; dst_ch < dst_channels; dst_ch++) {
      int16_t sample = 0;
      if (src_channels == 1 && dst_channels >= 1) {
        sample = src_frame_ptr[0];
      } else if (src_channels >= 2 && dst_channels == 1) {
        int mixed = 0;
        for (int src_ch = 0; src_ch < src_channels; src_ch++) mixed += src_frame_ptr[src_ch];
        sample = mixed / src_channels;
      } else {
        int src_ch = dst_ch < src_channels ? dst_ch : (src_channels - 1);
        sample = src_frame_ptr[src_ch];
      }
      dst[dst_frame * dst_channels + dst_ch] = sample;
    }
  }

  *out_buf = (uint8_t *)dst;
  *out_len = total_samples * sizeof(int16_t);
  return 0;
}

static Mix_Music *load_ogg_music_from_memory(const uint8_t *file_buf, int file_len) {
  int src_channels = 0;
  int src_rate = 0;
  short *pcm = NULL;
  int src_frames = stb_vorbis_decode_memory(file_buf, file_len, &src_channels, &src_rate, &pcm);
  if (src_frames < 0 || pcm == NULL) {
    set_error("stb_vorbis_decode_memory failed");
    return NULL;
  }

  Mix_Music *music = (Mix_Music *)calloc(1, sizeof(Mix_Music));
  assert(music != NULL);
  if (convert_pcm_s16(pcm, src_frames, src_rate, src_channels, &music->buf, &music->len) != 0) {
    free(pcm);
    free(music);
    return NULL;
  }
  free(pcm);
  return music;
}

static int16_t decode_wav_sample(const uint8_t *data, int bits_per_sample) {
  if (bits_per_sample == 8) {
    return ((int)data[0] - 128) << 8;
  }
  assert(bits_per_sample == 16);
  return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static Mix_Chunk *load_wav_chunk_from_memory(const uint8_t *file_buf, int file_len) {
  if (file_len < 12 || memcmp(file_buf, "RIFF", 4) != 0 || memcmp(file_buf + 8, "WAVE", 4) != 0) {
    set_error("not a wav file");
    return NULL;
  }

  const uint8_t *data_chunk = NULL;
  uint32_t data_size = 0;
  uint16_t audio_format = 0;
  uint16_t src_channels = 0;
  uint32_t src_rate = 0;
  uint16_t bits_per_sample = 0;

  int pos = 12;
  while (pos + 8 <= file_len) {
    const uint8_t *hdr = file_buf + pos;
    uint32_t chunk_size = read_le32(hdr + 4);
    int chunk_end = pos + 8 + chunk_size;
    if (chunk_end > file_len) break;

    if (memcmp(hdr, "fmt ", 4) == 0 && chunk_size >= 16) {
      audio_format = read_le16(hdr + 8);
      src_channels = read_le16(hdr + 10);
      src_rate = read_le32(hdr + 12);
      bits_per_sample = read_le16(hdr + 22);
    } else if (memcmp(hdr, "data", 4) == 0) {
      data_chunk = hdr + 8;
      data_size = chunk_size;
    }
    pos = chunk_end + (chunk_size & 1);
  }

  if (audio_format != 1 || data_chunk == NULL || data_size == 0 || src_channels <= 0 || src_rate <= 0) {
    set_error("unsupported wav format");
    return NULL;
  }
  if (bits_per_sample != 8 && bits_per_sample != 16) {
    set_error("unsupported wav bits per sample");
    return NULL;
  }

  int bytes_per_sample = bits_per_sample / 8;
  int src_frames = data_size / (src_channels * bytes_per_sample);
  int16_t *pcm = (int16_t *)malloc((uint32_t)src_frames * src_channels * sizeof(int16_t));
  assert(pcm != NULL);

  for (int frame = 0; frame < src_frames; frame++) {
    const uint8_t *frame_ptr = data_chunk + frame * src_channels * bytes_per_sample;
    for (int ch = 0; ch < src_channels; ch++) {
      pcm[frame * src_channels + ch] = decode_wav_sample(frame_ptr + ch * bytes_per_sample, bits_per_sample);
    }
  }

  Mix_Chunk *chunk = (Mix_Chunk *)calloc(1, sizeof(Mix_Chunk));
  assert(chunk != NULL);
  if (convert_pcm_s16(pcm, src_frames, src_rate, src_channels, &chunk->buf, &chunk->len) != 0) {
    free(pcm);
    free(chunk);
    return NULL;
  }
  free(pcm);
  return chunk;
}

static void advance_music_loop(void) {
  if (current_music == NULL) return;
  if (current_music->loops == -1) {
    current_music->pos = 0;
    return;
  }
  if (current_music->loops > 0) {
    current_music->loops--;
    current_music->pos = 0;
    return;
  }
  music_playing = 0;
  current_music = NULL;
  if (music_finished_cb != NULL) {
    music_finished_cb();
  }
}

static void mix_music_into_stream(uint8_t *stream, int len) {
  if (!music_playing || current_music == NULL) return;

  int mixed = 0;
  while (mixed < len && music_playing && current_music != NULL) {
    if (current_music->pos >= current_music->len) {
      advance_music_loop();
      if (!music_playing || current_music == NULL) break;
    }
    int remain = current_music->len - current_music->pos;
    int mix_len = len - mixed;
    if (mix_len > remain) mix_len = remain;

    memcpy(stream + mixed, current_music->buf + current_music->pos, mix_len);
    scale_s16_samples((int16_t *)(stream + mixed), mix_len / (int)sizeof(int16_t), music_volume);
    current_music->pos += mix_len;
    mixed += mix_len;
  }
}

static void mix_channels_into_stream(uint8_t *stream, int len) {
  if (channels == NULL) return;
  for (int channel = 0; channel < allocated_channels; channel++) {
    Mix_ChannelState *state = &channels[channel];
    if (!state->playing || state->chunk == NULL || state->volume <= 0) continue;

    int mixed = 0;
    while (mixed < len && state->playing && state->chunk != NULL) {
      if (state->pos >= state->chunk->len) {
        if (state->loops == -1) {
          state->pos = 0;
        } else if (state->loops > 0) {
          state->loops--;
          state->pos = 0;
        } else {
          state->playing = 0;
          state->chunk = NULL;
          state->pos = 0;
          if (channel_finished_cb != NULL) {
            channel_finished_cb(channel);
          }
          break;
        }
      }

      int remain = state->chunk->len - state->pos;
      int mix_len = len - mixed;
      if (mix_len > remain) mix_len = remain;
      SDL_MixAudio(stream + mixed, state->chunk->buf + state->pos, mix_len, state->volume);
      state->pos += mix_len;
      mixed += mix_len;
    }
  }
}

static void mixer_callback(void *userdata, uint8_t *stream, int len) {
  (void)userdata;
  memset(stream, 0, len);
  mix_music_into_stream(stream, len);
  mix_channels_into_stream(stream, len);
}

int Mix_OpenAudio(int frequency, uint16_t format, int channels_count, int chunksize) {
  SDL_AudioSpec desired = {};
  desired.freq = frequency;
  desired.format = format;
  desired.channels = channels_count;
  desired.samples = chunksize;
  desired.callback = mixer_callback;
  desired.userdata = NULL;

  if (SDL_OpenAudio(&desired, &mixer_spec) != 0) {
    set_error("SDL_OpenAudio failed");
    return -1;
  }

  mixer_opened = 1;
  music_playing = 0;
  current_music = NULL;
  music_volume = MIX_MAX_VOLUME;
  allocated_channels = 0;
  free(channels);
  channels = NULL;
  SDL_PauseAudio(0);
  return 0;
}

void Mix_CloseAudio() {
  mixer_opened = 0;
  music_playing = 0;
  current_music = NULL;
  free(channels);
  channels = NULL;
  allocated_channels = 0;
  memset(&mixer_spec, 0, sizeof(mixer_spec));
  SDL_CloseAudio();
}

char *Mix_GetError() {
  return mixer_error;
}

int Mix_QuerySpec(int *frequency, uint16_t *format, int *channels_count) {
  if (!mixer_opened) return 0;
  if (frequency) *frequency = mixer_spec.freq;
  if (format) *format = mixer_spec.format;
  if (channels_count) *channels_count = mixer_spec.channels;
  return 1;
}

Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc) {
  if (src == NULL) {
    set_error("NULL SDL_RWops");
    return NULL;
  }

  uint8_t *file_buf = NULL;
  int file_len = 0;
  int ret = read_rwops_all(src, &file_buf, &file_len);
  if (freesrc) SDL_RWclose(src);
  if (ret != 0) return NULL;

  Mix_Chunk *chunk = load_wav_chunk_from_memory(file_buf, file_len);
  free(file_buf);
  return chunk;
}

void Mix_FreeChunk(Mix_Chunk *chunk) {
  if (chunk == NULL) return;
  free(chunk->buf);
  free(chunk);
}

int Mix_AllocateChannels(int numchans) {
  if (numchans < 0) numchans = 0;
  free(channels);
  channels = NULL;
  allocated_channels = numchans;
  if (allocated_channels > 0) {
    channels = (Mix_ChannelState *)calloc(allocated_channels, sizeof(Mix_ChannelState));
    assert(channels != NULL);
    for (int i = 0; i < allocated_channels; i++) {
      channels[i].volume = MIX_MAX_VOLUME;
    }
  }
  return allocated_channels;
}

int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops) {
  if (channels == NULL || chunk == NULL || allocated_channels <= 0) return -1;

  if (channel == -1) {
    for (int i = 0; i < allocated_channels; i++) {
      if (!channels[i].playing) {
        channel = i;
        break;
      }
    }
  }
  if (channel < 0 || channel >= allocated_channels) return -1;

  channels[channel].chunk = chunk;
  channels[channel].pos = 0;
  channels[channel].loops = loops;
  channels[channel].playing = 1;
  return channel;
}

int Mix_Volume(int channel, int volume) {
  if (channels == NULL || allocated_channels <= 0) return 0;

  if (channel == -1) {
    for (int i = 0; i < allocated_channels; i++) {
      if (volume >= 0) {
        channels[i].volume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
      }
    }
    return volume;
  }

  if (channel < 0 || channel >= allocated_channels) return 0;
  int old = channels[channel].volume;
  if (volume >= 0) {
    channels[channel].volume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
  }
  return old;
}

void Mix_Pause(int channel) {
  if (channels == NULL || allocated_channels <= 0) return;

  if (channel == -1) {
    for (int i = 0; i < allocated_channels; i++) {
      channels[i].playing = 0;
      channels[i].chunk = NULL;
      channels[i].pos = 0;
    }
    return;
  }

  if (channel < 0 || channel >= allocated_channels) return;
  channels[channel].playing = 0;
  channels[channel].chunk = NULL;
  channels[channel].pos = 0;
}

void Mix_ChannelFinished(void (*channel_finished)(int channel)) {
  channel_finished_cb = channel_finished;
}

Mix_Music *Mix_LoadMUS(const char *file) {
  SDL_RWops *src = SDL_RWFromFile(file, "rb");
  if (src == NULL) {
    set_error("SDL_RWFromFile failed");
    return NULL;
  }
  return Mix_LoadMUS_RW(src);
}

Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src) {
  if (src == NULL) {
    set_error("NULL SDL_RWops");
    return NULL;
  }

  uint8_t *file_buf = NULL;
  int file_len = 0;
  int ret = read_rwops_all(src, &file_buf, &file_len);
  SDL_RWclose(src);
  if (ret != 0) return NULL;

  Mix_Music *music = load_ogg_music_from_memory(file_buf, file_len);
  free(file_buf);
  return music;
}

void Mix_FreeMusic(Mix_Music *music) {
  if (music == NULL) return;
  if (current_music == music) {
    music_playing = 0;
    current_music = NULL;
  }
  free(music->buf);
  free(music);
}

int Mix_PlayMusic(Mix_Music *music, int loops) {
  if (!mixer_opened || music == NULL) {
    set_error("music is not ready");
    return -1;
  }
  music->pos = 0;
  music->loops = loops;
  current_music = music;
  music_playing = 1;
  return 0;
}

int Mix_VolumeMusic(int volume) {
  int old = music_volume;
  if (volume >= 0) {
    music_volume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
  }
  return old;
}

int Mix_HaltMusic() {
  music_playing = 0;
  current_music = NULL;
  return 0;
}

int Mix_PlayingMusic() {
  return music_playing;
}

void Mix_HookMusicFinished(void (*music_finished)(void)) {
  music_finished_cb = music_finished;
}

int Mix_SetMusicPosition(double position) {
  (void)position;
  return 0;
}

int Mix_SetMusicCMD(const char *command) {
  (void)command;
  return 0;
}
