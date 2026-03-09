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
static int sbdev = -1;
static int sbctldev = -1;
static int screen_w = 0, screen_h = 0;
static int canvas_w = 0, canvas_h = 0;
static int canvas_x = 0, canvas_y = 0;
static int present_w = 0, present_h = 0;
static int present_x = 0, present_y = 0;
static uint32_t *stretch_buf = NULL;
static size_t stretch_buf_cap = 0;
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

static uint32_t *ensure_stretch_buf(size_t pixels) {
  if (pixels > stretch_buf_cap) {
    uint32_t *new_buf = realloc(stretch_buf, pixels * sizeof(uint32_t));
    assert(new_buf != NULL);
    stretch_buf = new_buf;
    stretch_buf_cap = pixels;
  }
  return stretch_buf;
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
  present_w = screen_w;
  present_h = screen_h;
  present_x = 0;
  present_y = 0;

  if (w != NULL && h != NULL) {
    if (*w == 0 && *h == 0) {
      *w = screen_w;
      *h = screen_h;
    }
    canvas_w = *w;
    canvas_h = *h;
  }

  if (getenv("NWM_APP")) {
    assert(canvas_w <= screen_w && canvas_h <= screen_h);
    present_w = canvas_w;
    present_h = canvas_h;
    present_x = (screen_w - present_w) / 2;
    present_y = (screen_h - present_h) / 2;

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

  canvas_x = present_x;
  canvas_y = present_y;
}

void NDL_DrawRect(uint32_t *pixels, int x, int y, int w, int h) {
  assert(fbdev >= 0);
  if (w <= 0 || h <= 0) return;

  if (present_w == canvas_w && present_h == canvas_h) {
    for (int row = 0; row < h; row++) {
      off_t offset = ((off_t)(present_y + y + row) * screen_w + present_x + x) * sizeof(uint32_t);
      lseek(fbdev, offset, SEEK_SET);
      write(fbdev, pixels + row * w, w * sizeof(uint32_t));
    }
    lseek(fbdev, 0, SEEK_END);
    write(fbdev, pixels, 0);
    return;
  }

  int dst_x0 = present_x + x * present_w / canvas_w;
  int dst_y0 = present_y + y * present_h / canvas_h;
  int dst_x1 = present_x + (x + w) * present_w / canvas_w;
  int dst_y1 = present_y + (y + h) * present_h / canvas_h;
  int dst_w = dst_x1 - dst_x0;
  int dst_h = dst_y1 - dst_y0;
  if (dst_w <= 0 || dst_h <= 0) return;

  uint32_t *buf = ensure_stretch_buf((size_t)dst_w * dst_h);
  for (int row = 0; row < dst_h; row++) {
    int src_row = row * h / dst_h;
    uint32_t *dst = buf + row * dst_w;
    uint32_t *src = pixels + src_row * w;
    for (int col = 0; col < dst_w; col++) {
      int src_col = col * w / dst_w;
      dst[col] = src[src_col];
    }
  }

  for (int row = 0; row < dst_h; row++) {
    off_t offset = ((off_t)(dst_y0 + row) * screen_w + dst_x0) * sizeof(uint32_t);
    lseek(fbdev, offset, SEEK_SET);
    write(fbdev, buf + row * dst_w, dst_w * sizeof(uint32_t));
  }
  lseek(fbdev, 0, SEEK_END);
  write(fbdev, buf, 0);
}

void NDL_OpenAudio(int freq, int channels, int samples) {
  if (sbdev < 0) {
    sbdev = open("/dev/sb", O_WRONLY, 0);
    assert(sbdev >= 0);
  }
  if (sbctldev < 0) {
    sbctldev = open("/dev/sbctl", O_RDWR, 0);
    assert(sbctldev >= 0);
  }

  int args[3] = { freq, channels, samples };
  int written = 0;
  while (written < (int)sizeof(args)) {
    int n = write(sbctldev, (uint8_t *)args + written, sizeof(args) - written);
    assert(n > 0);
    written += n;
  }
}

void NDL_CloseAudio() {
  if (sbdev >= 0) {
    close(sbdev);
    sbdev = -1;
  }
  if (sbctldev >= 0) {
    close(sbctldev);
    sbctldev = -1;
  }
}

int NDL_PlayAudio(void *buf, int len) {
  if (sbdev < 0 || len <= 0) return 0;
  int written = 0;
  while (written < len) {
    int n = write(sbdev, (uint8_t *)buf + written, len - written);
    if (n <= 0) break;
    written += n;
  }
  return written;
}

int NDL_QueryAudio() {
  if (sbctldev < 0) return 0;
  int free_bytes = 0;
  int nread = read(sbctldev, &free_bytes, sizeof(free_bytes));
  assert(nread == (int)sizeof(free_bytes));
  return free_bytes;
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
  NDL_CloseAudio();
  free(stretch_buf);
  stretch_buf = NULL;
  stretch_buf_cap = 0;
}
