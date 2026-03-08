#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <assert.h>

static int evtdev = -1;
static int fbdev = -1;
static int screen_w = 0, screen_h = 0;
static int canvas_w = 0, canvas_h = 0;
static int canvas_x = 0, canvas_y = 0;
static uint32_t boot_time = 0;

static void get_screen_size() {
  if (screen_w != 0 && screen_h != 0) return;

  int fd = open("/proc/dispinfo", 0, 0);
  assert(fd >= 0);

  char buf[64];
  int nread = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  assert(nread > 0);
  buf[nread] = '\0';

  char *width = strstr(buf, "WIDTH");
  char *height = strstr(buf, "HEIGHT");
  assert(width != NULL && height != NULL);
  sscanf(width, "WIDTH%*[^0-9]%d", &screen_w);
  sscanf(height, "HEIGHT%*[^0-9]%d", &screen_h);
}

uint32_t NDL_GetTicks() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint32_t now = (uint32_t)tv.tv_sec * 1000 + (uint32_t)(tv.tv_usec / 1000);
  return now - boot_time;
}

int NDL_PollEvent(char *buf, int len) {
  if (evtdev < 0 || len <= 0) return 0;

  int nread = read(evtdev, buf, len - 1);
  if (nread <= 0) return 0;
  if (buf[nread - 1] == '\n') nread --;
  buf[nread] = '\0';
  return nread;
}

void NDL_OpenCanvas(int *w, int *h) {
  get_screen_size();
  printf("screen size: %d x %d\n", screen_w, screen_h);

  canvas_w = screen_w;
  canvas_h = screen_h;
  if (w != NULL && h != NULL) {
    if (*w == 0 && *h == 0) {
      *w = screen_w;
      *h = screen_h;
    }
    assert(*w <= screen_w && *h <= screen_h);
    canvas_w = *w;
    canvas_h = *h;
  }
  canvas_x = (screen_w - canvas_w) / 2;
  canvas_y = (screen_h - canvas_h) / 2;

  if (getenv("NWM_APP")) {
    int fbctl = 4;
    fbdev = 5;
    char buf[64];
    int len = sprintf(buf, "%d %d", canvas_w, canvas_h);
    write(fbctl, buf, len);
    while (1) {
      int nread = read(3, buf, sizeof(buf) - 1);
      if (nread <= 0) continue;
      buf[nread] = '\0';
      if (strcmp(buf, "mmap ok") == 0) break;
    }
    close(fbctl);
  }
}

void NDL_DrawRect(uint32_t *pixels, int x, int y, int w, int h) {
  assert(fbdev >= 0);
  for (int row = 0; row < h; row++) {
    off_t offset = ((off_t)(canvas_y + y + row) * screen_w + canvas_x + x) * sizeof(uint32_t);
    lseek(fbdev, offset, SEEK_SET);
    write(fbdev, pixels + row * w, w * sizeof(uint32_t));
  }
  lseek(fbdev, 0, SEEK_END);
  write(fbdev, pixels, 0);
}

void NDL_OpenAudio(int freq, int channels, int samples) {
}

void NDL_CloseAudio() {
}

int NDL_PlayAudio(void *buf, int len) {
  return 0;
}

int NDL_QueryAudio() {
  return 0;
}

int NDL_Init(uint32_t flags) {
  (void)flags;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  boot_time = (uint32_t)tv.tv_sec * 1000 + (uint32_t)(tv.tv_usec / 1000);
  if (getenv("NWM_APP")) {
    evtdev = 3;
    fbdev = 5;
  } else {
    evtdev = open("/dev/events", 0, 0);
    fbdev = open("/dev/fb", 0, 0);
  }
  return 0;
}

void NDL_Quit() {
}
