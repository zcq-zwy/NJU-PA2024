#define SDL_malloc  malloc
#define SDL_free    free
#define SDL_realloc realloc

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define SDL_STBIMAGE_IMPLEMENTATION
#include "SDL_stbimage.h"

SDL_Surface* IMG_Load_RW(SDL_RWops *src, int freesrc) {
  if (src == NULL) return NULL;

  SDL_Surface *surface = NULL;
  if (src->type == RW_TYPE_MEM) {
    surface = STBIMG_LoadFromMemory((const unsigned char *)src->mem.base, src->mem.size);
  } else {
    int64_t size = SDL_RWsize(src);
    if (size > 0) {
      unsigned char *buf = (unsigned char *)malloc(size);
      if (buf != NULL) {
        SDL_RWseek(src, 0, RW_SEEK_SET);
        size_t nread = SDL_RWread(src, buf, 1, size);
        if (nread == (size_t)size) {
          surface = STBIMG_LoadFromMemory(buf, size);
        }
        free(buf);
      }
    }
  }

  if (freesrc) SDL_RWclose(src);
  return surface;
}

SDL_Surface* IMG_Load(const char *filename) {
  SDL_RWops *src = SDL_RWFromFile(filename, "rb");
  if (src == NULL) return NULL;
  return IMG_Load_RW(src, 1);
}

int IMG_isPNG(SDL_RWops *src) {
  static const unsigned char png_sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  unsigned char buf[8];
  int64_t cur = SDL_RWtell(src);
  size_t nread = SDL_RWread(src, buf, 1, sizeof(buf));
  SDL_RWseek(src, cur, RW_SEEK_SET);
  return nread == sizeof(buf) && memcmp(buf, png_sig, sizeof(buf)) == 0;
}

SDL_Surface* IMG_LoadJPG_RW(SDL_RWops *src) {
  return IMG_Load_RW(src, 0);
}

char *IMG_GetError() {
  return "Navy does not support IMG_GetError()";
}
