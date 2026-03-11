#include <SDL_mixer.h>
#include <stdlib.h>

static int mixer_opened = 0;
static int mixer_freq = 0;
static uint16_t mixer_format = 0;
static int mixer_channels = 0;
static int allocated_channels = 0;
static int music_playing = 0;
static int music_volume = MIX_MAX_VOLUME;
static void (*channel_finished_cb)(int channel) = NULL;
static void (*music_finished_cb)(void) = NULL;

int Mix_OpenAudio(int frequency, uint16_t format, int channels, int chunksize) {
  (void)chunksize;
  mixer_opened = 1;
  mixer_freq = frequency;
  mixer_format = format;
  mixer_channels = channels;
  music_playing = 0;
  return 0;
}

void Mix_CloseAudio() {
  mixer_opened = 0;
  music_playing = 0;
}

char *Mix_GetError() {
  return "";
}

int Mix_QuerySpec(int *frequency, uint16_t *format, int *channels) {
  if (!mixer_opened) return 0;
  if (frequency) *frequency = mixer_freq;
  if (format) *format = mixer_format;
  if (channels) *channels = mixer_channels;
  return 1;
}

Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc) {
  if (freesrc && src != NULL) SDL_RWclose(src);
  return (Mix_Chunk *)malloc(1);
}

void Mix_FreeChunk(Mix_Chunk *chunk) {
  free(chunk);
}

int Mix_AllocateChannels(int numchans) {
  allocated_channels = numchans;
  return allocated_channels;
}

int Mix_Volume(int channel, int volume) {
  (void)channel;
  return volume;
}

int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops) {
  (void)chunk;
  (void)loops;
  return channel;
}

void Mix_Pause(int channel) {
  (void)channel;
}

void Mix_ChannelFinished(void (*channel_finished)(int channel)) {
  channel_finished_cb = channel_finished;
  (void)channel_finished_cb;
}

Mix_Music *Mix_LoadMUS(const char *file) {
  (void)file;
  return (Mix_Music *)malloc(1);
}

Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src) {
  if (src != NULL) SDL_RWclose(src);
  return (Mix_Music *)malloc(1);
}

void Mix_FreeMusic(Mix_Music *music) {
  free(music);
}

int Mix_PlayMusic(Mix_Music *music, int loops) {
  (void)music;
  (void)loops;
  music_playing = 1;
  return 0;
}

int Mix_SetMusicPosition(double position) {
  (void)position;
  return 0;
}

int Mix_VolumeMusic(int volume) {
  int old = music_volume;
  music_volume = volume;
  return old;
}

int Mix_SetMusicCMD(const char *command) {
  (void)command;
  return 0;
}

int Mix_HaltMusic() {
  music_playing = 0;
  if (music_finished_cb != NULL) {
    music_finished_cb();
  }
  return 0;
}

void Mix_HookMusicFinished(void (*music_finished)()) {
  music_finished_cb = music_finished;
}

int Mix_PlayingMusic() {
  return music_playing;
}
