#include <NDL.h>
#include <sdl-video.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

void __SDL_AudioCallbackHelper(void);

void SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
  assert(dst && src);
  assert(dst->format->BitsPerPixel == src->format->BitsPerPixel);

  int src_x = (srcrect ? srcrect->x : 0);
  int src_y = (srcrect ? srcrect->y : 0);
  int width = (srcrect ? srcrect->w : src->w);
  int height = (srcrect ? srcrect->h : src->h);
  int dst_x = (dstrect ? dstrect->x : 0);
  int dst_y = (dstrect ? dstrect->y : 0);
  int bpp = src->format->BytesPerPixel;

  assert(src_x >= 0 && src_y >= 0 && dst_x >= 0 && dst_y >= 0);
  assert(src_x + width <= src->w && src_y + height <= src->h);
  assert(dst_x + width <= dst->w && dst_y + height <= dst->h);

  for (int row = 0; row < height; row++) {
    uint8_t *src_row = src->pixels + (src_y + row) * src->pitch + src_x * bpp;
    uint8_t *dst_row = dst->pixels + (dst_y + row) * dst->pitch + dst_x * bpp;
    memcpy(dst_row, src_row, width * bpp);
  }
}

void SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, uint32_t color) {
  assert(dst);

  int x = (dstrect ? dstrect->x : 0);
  int y = (dstrect ? dstrect->y : 0);
  int w = (dstrect ? dstrect->w : dst->w);
  int h = (dstrect ? dstrect->h : dst->h);

  assert(x >= 0 && y >= 0 && w >= 0 && h >= 0);
  assert(x + w <= dst->w && y + h <= dst->h);

  if (dst->format->BitsPerPixel == 32) {
    for (int row = 0; row < h; row++) {
      uint32_t *pixels = (uint32_t *)(dst->pixels + (y + row) * dst->pitch) + x;
      for (int col = 0; col < w; col++) {
        pixels[col] = color;
      }
    }
    return;
  }

  assert(dst->format->BitsPerPixel == 8);
  for (int row = 0; row < h; row++) {
    uint8_t *pixels = dst->pixels + (y + row) * dst->pitch + x;
    memset(pixels, color & 0xff, w);
  }
}

void SDL_UpdateRect(SDL_Surface *s, int x, int y, int w, int h) {
  __SDL_AudioCallbackHelper();
  assert(s);
  assert(s->flags & SDL_HWSURFACE);

  if (x == 0 && y == 0 && w == 0 && h == 0) {
    w = s->w;
    h = s->h;
  }
  assert(x >= 0 && y >= 0 && w >= 0 && h >= 0);
  assert(x + w <= s->w && y + h <= s->h);

  if (s->format->BitsPerPixel == 32) {
    if (x == 0 && w == s->w) {
      uint32_t *pixels = (uint32_t *)(s->pixels + y * s->pitch);
      NDL_DrawRect(pixels, x, y, w, h);
    } else {
      uint32_t *rectbuf = malloc(sizeof(uint32_t) * w * h);
      assert(rectbuf);
      for (int row = 0; row < h; row++) {
        uint32_t *src = (uint32_t *)(s->pixels + (y + row) * s->pitch) + x;
        memcpy(rectbuf + row * w, src, sizeof(uint32_t) * w);
      }
      NDL_DrawRect(rectbuf, x, y, w, h);
      free(rectbuf);
    }
    return;
  }

  assert(s->format->BitsPerPixel == 8);
  assert(s->format->palette != NULL);
  uint32_t *rectbuf = malloc(sizeof(uint32_t) * w * h);
  assert(rectbuf);

  for (int row = 0; row < h; row++) {
    uint8_t *src = s->pixels + (y + row) * s->pitch + x;
    for (int col = 0; col < w; col++) {
      SDL_Color c = s->format->palette->colors[src[col]];
      rectbuf[row * w + col] = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
    }
  }
  NDL_DrawRect(rectbuf, x, y, w, h);
  free(rectbuf);
}

// APIs below are already implemented.

static inline int maskToShift(uint32_t mask) {
  switch (mask) {
    case 0x000000ff: return 0;
    case 0x0000ff00: return 8;
    case 0x00ff0000: return 16;
    case 0xff000000: return 24;
    case 0x00000000: return 24; // hack
    default: assert(0);
  }
}

SDL_Surface* SDL_CreateRGBSurface(uint32_t flags, int width, int height, int depth,
    uint32_t Rmask, uint32_t Gmask, uint32_t Bmask, uint32_t Amask) {
  assert(depth == 8 || depth == 32);
  SDL_Surface *s = malloc(sizeof(SDL_Surface));
  assert(s);
  s->flags = flags;
  s->format = malloc(sizeof(SDL_PixelFormat));
  assert(s->format);
  if (depth == 8) {
    s->format->palette = malloc(sizeof(SDL_Palette));
    assert(s->format->palette);
    s->format->palette->colors = malloc(sizeof(SDL_Color) * 256);
    assert(s->format->palette->colors);
    memset(s->format->palette->colors, 0, sizeof(SDL_Color) * 256);
    s->format->palette->ncolors = 256;
  } else {
    s->format->palette = NULL;
    s->format->Rmask = Rmask; s->format->Rshift = maskToShift(Rmask); s->format->Rloss = 0;
    s->format->Gmask = Gmask; s->format->Gshift = maskToShift(Gmask); s->format->Gloss = 0;
    s->format->Bmask = Bmask; s->format->Bshift = maskToShift(Bmask); s->format->Bloss = 0;
    s->format->Amask = Amask; s->format->Ashift = maskToShift(Amask); s->format->Aloss = 0;
  }

  s->format->BitsPerPixel = depth;
  s->format->BytesPerPixel = depth / 8;

  s->w = width;
  s->h = height;
  s->pitch = width * depth / 8;
  assert(s->pitch == width * s->format->BytesPerPixel);

  if (!(flags & SDL_PREALLOC)) {
    s->pixels = malloc(s->pitch * height);
    assert(s->pixels);
  }

  return s;
}

SDL_Surface* SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth,
    int pitch, uint32_t Rmask, uint32_t Gmask, uint32_t Bmask, uint32_t Amask) {
  SDL_Surface *s = SDL_CreateRGBSurface(SDL_PREALLOC, width, height, depth,
      Rmask, Gmask, Bmask, Amask);
  assert(pitch == s->pitch);
  s->pixels = pixels;
  return s;
}

void SDL_FreeSurface(SDL_Surface *s) {
  if (s != NULL) {
    if (s->format != NULL) {
      if (s->format->palette != NULL) {
        if (s->format->palette->colors != NULL) free(s->format->palette->colors);
        free(s->format->palette);
      }
      free(s->format);
    }
    if (s->pixels != NULL && !(s->flags & SDL_PREALLOC)) free(s->pixels);
    free(s);
  }
}

SDL_Surface* SDL_SetVideoMode(int width, int height, int bpp, uint32_t flags) {
  if (flags & SDL_HWSURFACE) NDL_OpenCanvas(&width, &height);
  return SDL_CreateRGBSurface(flags, width, height, bpp,
      DEFAULT_RMASK, DEFAULT_GMASK, DEFAULT_BMASK, DEFAULT_AMASK);
}

void SDL_SoftStretch(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
  assert(src && dst);
  assert(dst->format->BitsPerPixel == src->format->BitsPerPixel);
  assert(dst->format->BitsPerPixel == 8);

  int src_x = (srcrect == NULL ? 0 : srcrect->x);
  int src_y = (srcrect == NULL ? 0 : srcrect->y);
  int src_w = (srcrect == NULL ? src->w : srcrect->w);
  int src_h = (srcrect == NULL ? src->h : srcrect->h);

  assert(dstrect);
  assert(src_x >= 0 && src_y >= 0 && src_w >= 0 && src_h >= 0);
  assert(src_x + src_w <= src->w && src_y + src_h <= src->h);
  assert(dstrect->x >= 0 && dstrect->y >= 0 && dstrect->w >= 0 && dstrect->h >= 0);
  assert(dstrect->x + dstrect->w <= dst->w && dstrect->y + dstrect->h <= dst->h);

  if (src_w == dstrect->w && src_h == dstrect->h) {
    /* The source rectangle and the destination rectangle
     * are of the same size. If that is the case, there
     * is no need to stretch, just copy. */
    SDL_Rect rect;
    rect.x = src_x;
    rect.y = src_y;
    rect.w = src_w;
    rect.h = src_h;
    SDL_BlitSurface(src, &rect, dst, dstrect);
    return;
  }

  for (int dy = 0; dy < dstrect->h; dy++) {
    int sy = src_y + dy * src_h / dstrect->h;
    uint8_t *dst_row = dst->pixels + (dstrect->y + dy) * dst->pitch + dstrect->x;
    uint8_t *src_row = src->pixels + sy * src->pitch + src_x;
    for (int dx = 0; dx < dstrect->w; dx++) {
      int sx = dx * src_w / dstrect->w;
      dst_row[dx] = src_row[sx];
    }
  }
}

void SDL_SetPalette(SDL_Surface *s, int flags, SDL_Color *colors, int firstcolor, int ncolors) {
  assert(s);
  assert(s->format);
  assert(s->format->palette);
  assert(firstcolor == 0);

  s->format->palette->ncolors = ncolors;
  memcpy(s->format->palette->colors, colors, sizeof(SDL_Color) * ncolors);

  if(s->flags & SDL_HWSURFACE) {
    assert(ncolors == 256);
    for (int i = 0; i < ncolors; i ++) {
      uint8_t r = colors[i].r;
      uint8_t g = colors[i].g;
      uint8_t b = colors[i].b;
    }
    SDL_UpdateRect(s, 0, 0, 0, 0);
  }
}

static void ConvertPixelsARGB_ABGR(void *dst, void *src, int len) {
  int i;
  uint8_t (*pdst)[4] = dst;
  uint8_t (*psrc)[4] = src;
  union {
    uint8_t val8[4];
    uint32_t val32;
  } tmp;
  int first = len & ~0xf;
  for (i = 0; i < first; i += 16) {
#define macro(i) \
    tmp.val32 = *((uint32_t *)psrc[i]); \
    *((uint32_t *)pdst[i]) = tmp.val32; \
    pdst[i][0] = tmp.val8[2]; \
    pdst[i][2] = tmp.val8[0];

    macro(i + 0); macro(i + 1); macro(i + 2); macro(i + 3);
    macro(i + 4); macro(i + 5); macro(i + 6); macro(i + 7);
    macro(i + 8); macro(i + 9); macro(i +10); macro(i +11);
    macro(i +12); macro(i +13); macro(i +14); macro(i +15);
  }

  for (; i < len; i ++) {
    macro(i);
  }
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, SDL_PixelFormat *fmt, uint32_t flags) {
  assert(src->format->BitsPerPixel == 32);
  assert(src->w * src->format->BytesPerPixel == src->pitch);
  assert(src->format->BitsPerPixel == fmt->BitsPerPixel);

  SDL_Surface* ret = SDL_CreateRGBSurface(flags, src->w, src->h, fmt->BitsPerPixel,
    fmt->Rmask, fmt->Gmask, fmt->Bmask, fmt->Amask);

  assert(fmt->Gmask == src->format->Gmask);
  assert(fmt->Amask == 0 || src->format->Amask == 0 || (fmt->Amask == src->format->Amask));
  ConvertPixelsARGB_ABGR(ret->pixels, src->pixels, src->w * src->h);

  return ret;
}

uint32_t SDL_MapRGBA(SDL_PixelFormat *fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  assert(fmt->BytesPerPixel == 4);
  uint32_t p = (r << fmt->Rshift) | (g << fmt->Gshift) | (b << fmt->Bshift);
  if (fmt->Amask) p |= (a << fmt->Ashift);
  return p;
}

int SDL_LockSurface(SDL_Surface *s) {
  return 0;
}

void SDL_UnlockSurface(SDL_Surface *s) {
}
