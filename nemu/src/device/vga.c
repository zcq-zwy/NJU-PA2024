/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <common.h>
#include <device/map.h>
#include <stdio.h>

#define SCREEN_W (MUXDEF(CONFIG_VGA_SIZE_800x600, 800, 400))
#define SCREEN_H (MUXDEF(CONFIG_VGA_SIZE_800x600, 600, 300))

static uint32_t screen_width() {
  return MUXDEF(CONFIG_TARGET_AM, io_read(AM_GPU_CONFIG).width, SCREEN_W);
}

static uint32_t screen_height() {
  return MUXDEF(CONFIG_TARGET_AM, io_read(AM_GPU_CONFIG).height, SCREEN_H);
}

static uint32_t screen_size() {
  return screen_width() * screen_height() * sizeof(uint32_t);
}

static void *vmem = NULL;
static uint32_t *vgactl_port_base = NULL;

#ifdef CONFIG_VGA_SHOW_SCREEN
#ifndef CONFIG_TARGET_AM
#include <SDL2/SDL.h>

static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;

static const char *renderer_policy_name() {
#ifdef CONFIG_VGA_RENDERER_SOFTWARE
  return "software";
#elif defined(CONFIG_VGA_RENDERER_ACCELERATED)
  return "accelerated";
#else
  return "auto";
#endif
}

static uint32_t renderer_flags() {
#ifdef CONFIG_VGA_RENDERER_SOFTWARE
  return SDL_RENDERER_SOFTWARE;
#elif defined(CONFIG_VGA_RENDERER_ACCELERATED)
  return SDL_RENDERER_ACCELERATED;
#else
  return 0;
#endif
}

static void init_screen() {
  SDL_Window *window = NULL;
  SDL_RendererInfo info = {};
  char title[128];
  sprintf(title, "%s-NEMU", str(__GUEST_ISA__));
  SDL_Init(SDL_INIT_VIDEO);
  window = SDL_CreateWindow(
      title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      SCREEN_W * (MUXDEF(CONFIG_VGA_SIZE_400x300, 2, 1)),
      SCREEN_H * (MUXDEF(CONFIG_VGA_SIZE_400x300, 2, 1)), 0);
  Assert(window != NULL, "Can not create SDL window: %s", SDL_GetError());

#ifdef CONFIG_VGA_RENDERER_SOFTWARE
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
#else
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
#endif

  renderer = SDL_CreateRenderer(window, -1, renderer_flags());
  if (renderer == NULL && renderer_flags() != 0) {
    Log("Requested SDL renderer policy '%s' failed: %s; fallback to auto",
        renderer_policy_name(), SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
    renderer = SDL_CreateRenderer(window, -1, 0);
  }
  Assert(renderer != NULL, "Can not create SDL renderer: %s", SDL_GetError());
  SDL_SetWindowTitle(window, title);

  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STATIC, SCREEN_W, SCREEN_H);
  Assert(texture != NULL, "Can not create SDL texture: %s", SDL_GetError());

  if (SDL_GetRendererInfo(renderer, &info) == 0) {
    Log("VGA SDL renderer policy=%s actual=%s flags=0x%x",
        renderer_policy_name(), info.name, info.flags);
  }
  SDL_RenderPresent(renderer);
}

static inline void update_screen() {
  SDL_UpdateTexture(texture, NULL, vmem, SCREEN_W * sizeof(uint32_t));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}

#else
static void init_screen() {}

static inline void update_screen() {
  io_write(AM_GPU_FBDRAW, 0, 0, vmem, screen_width(), screen_height(), true);
}
#endif
#endif

void vga_update_screen() {
  if (vgactl_port_base[1] != 0) {
    IFDEF(CONFIG_VGA_SHOW_SCREEN, update_screen());
    vgactl_port_base[1] = 0;
  }
}

void init_vga() {
  vgactl_port_base = (uint32_t *)new_space(8);
  vgactl_port_base[0] = (screen_width() << 16) | screen_height();
  vgactl_port_base[1] = 0;
#ifdef CONFIG_HAS_PORT_IO
  add_pio_map ("vgactl", CONFIG_VGA_CTL_PORT, vgactl_port_base, 8, NULL);
#else
  add_mmio_map("vgactl", CONFIG_VGA_CTL_MMIO, vgactl_port_base, 8, NULL);
#endif

  vmem = new_space(screen_size());
  add_mmio_map("vmem", CONFIG_FB_ADDR, vmem, screen_size(), NULL);
  IFDEF(CONFIG_VGA_SHOW_SCREEN, init_screen());
  IFDEF(CONFIG_VGA_SHOW_SCREEN, memset(vmem, 0, screen_size()));
}
