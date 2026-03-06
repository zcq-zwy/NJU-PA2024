#include <am.h>
#include <nemu.h>

#define SYNC_ADDR (VGACTL_ADDR + 4)

static int screen_w = 0;
static int screen_h = 0;

void __am_gpu_init() {
  uint32_t vga_size = inl(VGACTL_ADDR);
  screen_w = vga_size >> 16;
  screen_h = vga_size & 0xffff;
}

void __am_gpu_config(AM_GPU_CONFIG_T *cfg) {
  if (screen_w == 0 || screen_h == 0) {
    uint32_t vga_size = inl(VGACTL_ADDR);
    screen_w = vga_size >> 16;
    screen_h = vga_size & 0xffff;
  }

  *cfg = (AM_GPU_CONFIG_T) {
    .present = true, .has_accel = false,
    .width = screen_w, .height = screen_h,
    .vmemsz = screen_w * screen_h * sizeof(uint32_t)
  };
}

void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *ctl) {
  if (ctl->w > 0 && ctl->h > 0) {
    uint32_t *fb = (uint32_t *)(uintptr_t)FB_ADDR;
    const uint32_t *pixels = (const uint32_t *)ctl->pixels;

    for (int y = 0; y < ctl->h; y++) {
      int dst_row = (ctl->y + y) * screen_w + ctl->x;
      int src_row = y * ctl->w;
      for (int x = 0; x < ctl->w; x++) {
        fb[dst_row + x] = pixels[src_row + x];
      }
    }
  }

  if (ctl->sync) {
    outl(SYNC_ADDR, 1);
  }
}

void __am_gpu_status(AM_GPU_STATUS_T *status) {
  status->ready = true;
}
