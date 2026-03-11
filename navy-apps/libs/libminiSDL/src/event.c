#include <NDL.h>
#include <SDL.h>
#include <stdio.h>
#include <string.h>

void __SDL_AudioCallbackHelper(void);
void __SDL_TimerCallbackHelper(void);

#define keyname(k) #k,

static const char *keyname[] = {
  "NONE",
  _KEYS(keyname)
};

static uint8_t keystate[sizeof(keyname) / sizeof(keyname[0])] = {};

#define EVENT_QUEUE_LEN 64
static SDL_Event event_queue[EVENT_QUEUE_LEN] = {};
static int event_head = 0;
static int event_tail = 0;

static int queue_empty(void) {
  return event_head == event_tail;
}

static int queue_next(int index) {
  return (index + 1) % EVENT_QUEUE_LEN;
}

static int queue_push(const SDL_Event *ev) {
  int next = queue_next(event_tail);
  if (next == event_head) return 0;
  event_queue[event_tail] = *ev;
  event_tail = next;
  return 1;
}

static int queue_pop(SDL_Event *ev) {
  if (queue_empty()) return 0;
  if (ev != NULL) *ev = event_queue[event_head];
  event_head = queue_next(event_head);
  return 1;
}

static int lookup_key(const char *name) {
  for (int i = 0; i < (int)(sizeof(keyname) / sizeof(keyname[0])); i++) {
    if (strcmp(keyname[i], name) == 0) {
      return i;
    }
  }
  return SDLK_NONE;
}

int SDL_PushEvent(SDL_Event *ev) {
  if (ev == NULL) return -1;
  return queue_push(ev) ? 1 : -1;
}

static int pump_ndl_event(void) {
  char buf[64];
  int nread = NDL_PollEvent(buf, sizeof(buf));
  if (nread == 0) return 0;

  SDL_Event ev = {};
  char type[3] = {};
  char name[32] = {};
  if (sscanf(buf, "%2s %31s", type, name) != 2) return 0;

  int key = lookup_key(name);
  if (key == SDLK_NONE) return 0;

  if (strcmp(type, "kd") == 0) {
    ev.type = SDL_KEYDOWN;
    ev.key.type = SDL_KEYDOWN;
    ev.key.keysym.sym = key;
    keystate[key] = 1;
    return queue_push(&ev);
  }
  if (strcmp(type, "ku") == 0) {
    ev.type = SDL_KEYUP;
    ev.key.type = SDL_KEYUP;
    ev.key.keysym.sym = key;
    keystate[key] = 0;
    return queue_push(&ev);
  }
  return 0;
}

static void pump_events(void) {
  __SDL_AudioCallbackHelper();
  __SDL_TimerCallbackHelper();
  while (pump_ndl_event()) {}
}

int SDL_PollEvent(SDL_Event *ev) {
  pump_events();
  return queue_pop(ev);
}

int SDL_WaitEvent(SDL_Event *event) {
  while (1) {
    pump_events();
    if (SDL_PollEvent(event)) return 1;
  }
}

int SDL_PeepEvents(SDL_Event *ev, int numevents, int action, uint32_t mask) {
  if (numevents <= 0) return 0;
  if (action == SDL_ADDEVENT) {
    int added = 0;
    for (int i = 0; i < numevents; i++) {
      if (SDL_PushEvent(&ev[i]) < 0) break;
      added++;
    }
    return added;
  }

  pump_events();

  SDL_Event kept[EVENT_QUEUE_LEN];
  int kept_nr = 0, matched_nr = 0;

  while (!queue_empty()) {
    SDL_Event cur;
    queue_pop(&cur);
    int matched = (cur.type < 32) && (mask & SDL_EVENTMASK(cur.type));
    if (matched && matched_nr < numevents) {
      if (ev != NULL) ev[matched_nr] = cur;
      matched_nr++;
      if (action == SDL_PEEKEVENT) {
        kept[kept_nr++] = cur;
      }
    } else {
      kept[kept_nr++] = cur;
    }
  }

  event_head = event_tail = 0;
  for (int i = 0; i < kept_nr; i++) {
    queue_push(&kept[i]);
  }

  return matched_nr;
}

uint8_t* SDL_GetKeyState(int *numkeys) {
  if (numkeys) {
    *numkeys = sizeof(keystate) / sizeof(keystate[0]);
  }
  return keystate;
}
