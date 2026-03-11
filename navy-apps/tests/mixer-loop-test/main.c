#include <SDL.h>
#include <SDL_mixer.h>

#include <stdio.h>

#define MUSIC_PATH "/share/music/little-star.ogg"

int main(void) {
  if (SDL_Init(0) != 0) {
    printf("SDL_Init failed\n");
    return 1;
  }

  if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 4096) != 0) {
    printf("Mix_OpenAudio failed: %s\n", Mix_GetError());
    return 1;
  }

  Mix_Music *music = Mix_LoadMUS(MUSIC_PATH);
  if (music == NULL) {
    printf("Mix_LoadMUS failed: %s\n", Mix_GetError());
    return 1;
  }

  if (Mix_PlayMusic(music, -1) != 0) {
    printf("Mix_PlayMusic failed: %s\n", Mix_GetError());
    return 1;
  }

  printf("loop test started: %s\n", MUSIC_PATH);
  fflush(stdout);

  SDL_Delay(55000);

  Mix_HaltMusic();
  Mix_FreeMusic(music);
  Mix_CloseAudio();
  SDL_Quit();
  return 0;
}
