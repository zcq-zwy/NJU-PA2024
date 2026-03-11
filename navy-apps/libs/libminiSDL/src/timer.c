#include <NDL.h>
#include <sdl-timer.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_TIMER_NR 16

typedef struct {
  bool used;
  uint32_t interval;
  uint32_t expire;
  SDL_NewTimerCallback callback;
  void *param;
} TimerSlot;

static TimerSlot timers[MAX_TIMER_NR] = {};

void __SDL_AudioCallbackHelper(void);
void __SDL_TimerCallbackHelper(void) {
  uint32_t now = SDL_GetTicks();

  for (int i = 0; i < MAX_TIMER_NR; i++) {
    if (!timers[i].used) continue;

    if ((int32_t)(now - timers[i].expire) < 0) continue;

    SDL_NewTimerCallback callback = timers[i].callback;
    void *param = timers[i].param;
    uint32_t interval = timers[i].interval;

    timers[i].used = false;
    uint32_t next = callback(interval, param);
    if (next != 0) {
      timers[i].used = true;
      timers[i].interval = next;
      timers[i].expire = now + next;
      timers[i].callback = callback;
      timers[i].param = param;
    }

    now = SDL_GetTicks();
  }
}

SDL_TimerID SDL_AddTimer(uint32_t interval, SDL_NewTimerCallback callback, void *param) {
  if (interval == 0 || callback == NULL) return NULL;

  __SDL_TimerCallbackHelper();

  for (int i = 0; i < MAX_TIMER_NR; i++) {
    if (timers[i].used) continue;
    timers[i].used = true;
    timers[i].interval = interval;
    timers[i].expire = SDL_GetTicks() + interval;
    timers[i].callback = callback;
    timers[i].param = param;
    return &timers[i];
  }

  return NULL;
}

int SDL_RemoveTimer(SDL_TimerID id) {
  if (id == NULL) return 0;

  TimerSlot *timer = (TimerSlot *)id;
  if (timer < timers || timer >= timers + MAX_TIMER_NR || !timer->used) {
    return 0;
  }

  timer->used = false;
  return 1;
}

uint32_t SDL_GetTicks() {
  return NDL_GetTicks();
}

void SDL_Delay(uint32_t ms) {
  uint32_t start = SDL_GetTicks();
  while (SDL_GetTicks() - start < ms) {
    __SDL_AudioCallbackHelper();
    __SDL_TimerCallbackHelper();
  }
}
