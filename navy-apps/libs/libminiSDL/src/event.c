#include <NDL.h>
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define keyname(k) #k,

static const char *keyname[] = {
  "NONE",
  _KEYS(keyname)
};

static uint8_t keystate[sizeof(keyname) / sizeof(keyname[0])] = {};

static int lookup_key(const char *name) {
  for (int i = 0; i < (int)(sizeof(keyname) / sizeof(keyname[0])); i++) {
    if (strcmp(keyname[i], name) == 0) {
      return i;
    }
  }
  return SDLK_NONE;
}

int SDL_PushEvent(SDL_Event *ev) {
  return 0;
}

int SDL_PollEvent(SDL_Event *ev) {
  char buf[64];
  int nread = NDL_PollEvent(buf, sizeof(buf));
  if (nread == 0) return 0;

  char type[3] = {};
  char name[32] = {};
  if (sscanf(buf, "%2s %31s", type, name) != 2) return 0;

  int key = lookup_key(name);
  if (key == SDLK_NONE) return 0;

  if (strcmp(type, "kd") == 0) {
    ev->type = SDL_KEYDOWN;
    ev->key.type = SDL_KEYDOWN;
    ev->key.keysym.sym = key;
    keystate[key] = 1;
    return 1;
  }
  if (strcmp(type, "ku") == 0) {
    ev->type = SDL_KEYUP;
    ev->key.type = SDL_KEYUP;
    ev->key.keysym.sym = key;
    keystate[key] = 0;
    return 1;
  }
  return 0;
}

int SDL_WaitEvent(SDL_Event *event) {
  while (1) {
    if (SDL_PollEvent(event)) return 1;
  }
}

int SDL_PeepEvents(SDL_Event *ev, int numevents, int action, uint32_t mask) {
  return 0;
}

uint8_t* SDL_GetKeyState(int *numkeys) {
  if (numkeys) {
    *numkeys = sizeof(keystate) / sizeof(keystate[0]);
  }
  return keystate;
}
