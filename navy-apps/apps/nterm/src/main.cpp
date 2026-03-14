#include <nterm.h>
#include <SDL.h>
#include <SDL_bdf.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

struct FontChoice {
  const char *path;
  int w;
  int h;
};

static const FontChoice font_choices[] = {
  { "/share/fonts/Courier-7.bdf",  7, 13 },
  { "/share/fonts/Courier-8.bdf",  8, 15 },
  { "/share/fonts/Courier-9.bdf",  9, 15 },
  { "/share/fonts/Courier-10.bdf", 10, 17 },
  { "/share/fonts/Courier-11.bdf", 11, 20 },
  { "/share/fonts/Courier-12.bdf", 12, 20 },
  { "/share/fonts/Courier-13.bdf", 13, 23 },
};

static const char *font_fname = "/share/fonts/Courier-7.bdf";
static const char *boot_music_fname = "/share/music/boot.wav";
static BDF_Font *font = NULL;
static SDL_Surface *screen = NULL;
Terminal *term = NULL;

static int screen_w = 0;
static int screen_h = 0;
static int cell_w = 0;
static int cell_h = 0;
static int term_x = 0;
static int term_y = 0;
static int cell_x[W + 1] = {};
static int cell_y[H + 1] = {};

static uint8_t *boot_audio_buf = NULL;
static uint32_t boot_audio_len = 0;
static uint32_t boot_audio_pos = 0;
static int boot_audio_playing = 0;
static int boot_audio_done = 0;

void builtin_sh_run();
void extern_app_run(const char *app_path);

static void read_screen_size() {
  if (screen_w != 0 && screen_h != 0) return;

  int fd = open("/proc/dispinfo", O_RDONLY, 0);
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

static void select_font() {
  read_screen_size();

  int best = 0;
  int best_area = 0;
  int best_scale = 0;

  for (int i = 0; i < (int)(sizeof(font_choices) / sizeof(font_choices[0])); i++) {
    const FontChoice &choice = font_choices[i];
    int scale_x = screen_w / (W * choice.w);
    int scale_y = screen_h / (H * choice.h);
    int scale = (scale_x < scale_y ? scale_x : scale_y);
    if (scale < 1) continue;

    int area = (W * choice.w * scale) * (H * choice.h * scale);
    if (area > best_area ||
        (area == best_area && scale > best_scale) ||
        (area == best_area && scale == best_scale && choice.h > font_choices[best].h)) {
      best = i;
      best_area = area;
      best_scale = scale;
    }
  }

  font_fname = font_choices[best].path;
}

static void init_char_layout() {
  read_screen_size();
  assert(font != NULL);

  int scale_x = screen_w / (W * font->w);
  int scale_y = screen_h / (H * font->h);
  int scale = (scale_x < scale_y ? scale_x : scale_y);
  if (scale < 1) scale = 1;

  cell_w = font->w * scale;
  cell_h = font->h * scale;
  term_x = (screen_w - cell_w * W) / 2;
  term_y = (screen_h - cell_h * H) / 2;

  for (int i = 0; i <= W; i++) cell_x[i] = term_x + i * cell_w;
  for (int j = 0; j <= H; j++) cell_y[j] = term_y + j * cell_h;
}

static void blit_scaled_surface(SDL_Surface *src, int dst_x, int dst_y, int dst_w, int dst_h) {
  assert(src != NULL);
  assert(src->format->BitsPerPixel == 32);
  assert(screen != NULL && screen->format->BitsPerPixel == 32);
  if (dst_w <= 0 || dst_h <= 0) return;

  uint32_t *src_pixels = (uint32_t *)src->pixels;
  uint32_t *dst_pixels = (uint32_t *)screen->pixels;
  int dst_pitch = screen->pitch / (int)sizeof(uint32_t);

  for (int y = 0; y < dst_h; y++) {
    int sy = y * src->h / dst_h;
    uint32_t *dst_row = dst_pixels + (dst_y + y) * dst_pitch + dst_x;
    uint32_t *src_row = src_pixels + sy * src->w;
    for (int x = 0; x < dst_w; x++) {
      int sx = x * src->w / dst_w;
      dst_row[x] = src_row[sx];
    }
  }
}

static void boot_audio_callback(void *userdata, uint8_t *stream, int len) {
  memset(stream, 0, len);
  if (!boot_audio_playing || boot_audio_buf == NULL || boot_audio_pos >= boot_audio_len) {
    return;
  }

  uint32_t remain = boot_audio_len - boot_audio_pos;
  uint32_t ncopy = (remain < (uint32_t)len ? remain : (uint32_t)len);
  memcpy(stream, boot_audio_buf + boot_audio_pos, ncopy);
  boot_audio_pos += ncopy;
  if (boot_audio_pos >= boot_audio_len) {
    boot_audio_done = 1;
  }
}

static void boot_audio_start() {
  SDL_AudioSpec spec = {};
  if (SDL_LoadWAV(boot_music_fname, &spec, &boot_audio_buf, &boot_audio_len) == NULL) {
    return;
  }

  spec.callback = boot_audio_callback;
  spec.userdata = NULL;
  boot_audio_pos = 0;
  boot_audio_done = 0;
  boot_audio_playing = 1;
  SDL_OpenAudio(&spec, NULL);
  SDL_PauseAudio(0);
}

static void boot_audio_stop() {
  if (!boot_audio_playing && boot_audio_buf == NULL) {
    return;
  }

  SDL_CloseAudio();
  if (boot_audio_buf != NULL) {
    SDL_FreeWAV(boot_audio_buf);
  }
  boot_audio_buf = NULL;
  boot_audio_len = 0;
  boot_audio_pos = 0;
  boot_audio_playing = 0;
  boot_audio_done = 0;
}

int main(int argc, char *argv[]) {
  SDL_Init(0);
  select_font();
  font = new BDF_Font(font_fname);
  init_char_layout();

  screen = SDL_SetVideoMode(screen_w, screen_h, 32, SDL_HWSURFACE);
  SDL_FillRect(screen, NULL, 0);
  SDL_UpdateRect(screen, 0, 0, 0, 0);

  term = new Terminal(W, H);

  if (argc < 2) {
    if (getenv("NWM_APP") == NULL) {
      boot_audio_start();
    }
    builtin_sh_run();
  }
  else { extern_app_run(argv[1]); }

  assert(0);
}

static void draw_ch(int col, int row, char ch, uint32_t fg, uint32_t bg) {
  SDL_Surface *s = BDF_CreateSurface(font, ch, fg, bg);
  if (s == NULL) return;
  int x0 = cell_x[col], y0 = cell_y[row];
  int x1 = cell_x[col + 1], y1 = cell_y[row + 1];
  blit_scaled_surface(s, x0, y0, x1 - x0, y1 - y0);
  SDL_FreeSurface(s);
}

void refresh_terminal() {
  if (boot_audio_done) {
    boot_audio_stop();
  }

  int needsync = 0;
  for (int i = 0; i < W; i ++)
    for (int j = 0; j < H; j ++)
      if (term->is_dirty(i, j)) {
        draw_ch(i, j, term->getch(i, j), term->foreground(i, j), term->background(i, j));
        needsync = 1;
      }
  term->clear();

  static uint32_t last = 0;
  static int flip = 0;
  uint32_t now = SDL_GetTicks();
  if (now - last > 500 || needsync) {
    int x = term->cursor.x, y = term->cursor.y;
    uint32_t color = (flip ? term->foreground(x, y) : term->background(x, y));
    draw_ch(x, y, ' ', 0, color);
    SDL_UpdateRect(screen, 0, 0, 0, 0);
    if (now - last > 500) {
      flip = !flip;
      last = now;
    }
  }
}

#define ENTRY(KEYNAME, NOSHIFT, SHIFT) { SDLK_##KEYNAME, #KEYNAME, NOSHIFT, SHIFT }
static const struct {
  int keycode;
  const char *name;
  char noshift, shift;
} SHIFT[] = {
  ENTRY(ESCAPE,       '\033', '\033'),
  ENTRY(SPACE,        ' ' , ' '),
  ENTRY(RETURN,       '\n', '\n'),
  ENTRY(BACKSPACE,    '\b', '\b'),
  ENTRY(1,            '1',  '!'),
  ENTRY(2,            '2',  '@'),
  ENTRY(3,            '3',  '#'),
  ENTRY(4,            '4',  '$'),
  ENTRY(5,            '5',  '%'),
  ENTRY(6,            '6',  '^'),
  ENTRY(7,            '7',  '&'),
  ENTRY(8,            '8',  '*'),
  ENTRY(9,            '9',  '('),
  ENTRY(0,            '0',  ')'),
  ENTRY(GRAVE,        '`',  '~'),
  ENTRY(MINUS,        '-',  '_'),
  ENTRY(EQUALS,       '=',  '+'),
  ENTRY(SEMICOLON,    ';',  ':'),
  ENTRY(APOSTROPHE,   '\'', '"'),
  ENTRY(LEFTBRACKET,  '[',  '{'),
  ENTRY(RIGHTBRACKET, ']',  '}'),
  ENTRY(BACKSLASH,    '\\', '|'),
  ENTRY(COMMA,        ',',  '<'),
  ENTRY(PERIOD,       '.',  '>'),
  ENTRY(SLASH,        '/',  '?'),
  ENTRY(A,            'a',  'A'),
  ENTRY(B,            'b',  'B'),
  ENTRY(C,            'c',  'C'),
  ENTRY(D,            'd',  'D'),
  ENTRY(E,            'e',  'E'),
  ENTRY(F,            'f',  'F'),
  ENTRY(G,            'g',  'G'),
  ENTRY(H,            'h',  'H'),
  ENTRY(I,            'i',  'I'),
  ENTRY(J,            'j',  'J'),
  ENTRY(K,            'k',  'K'),
  ENTRY(L,            'l',  'L'),
  ENTRY(M,            'm',  'M'),
  ENTRY(N,            'n',  'N'),
  ENTRY(O,            'o',  'O'),
  ENTRY(P,            'p',  'P'),
  ENTRY(Q,            'q',  'Q'),
  ENTRY(R,            'r',  'R'),
  ENTRY(S,            's',  'S'),
  ENTRY(T,            't',  'T'),
  ENTRY(U,            'u',  'U'),
  ENTRY(V,            'v',  'V'),
  ENTRY(W,            'w',  'W'),
  ENTRY(X,            'x',  'X'),
  ENTRY(Y,            'y',  'Y'),
  ENTRY(Z,            'z',  'Z'),
};

char handle_key(const char *buf) {
  char key[32];
  static int shift = 0;
  sscanf(buf + 2, "%s", key);

  if (strcmp(key, "LSHIFT") == 0 || strcmp(key, "RSHIFT") == 0)  { shift ^= 1; return '\0'; }

  if (buf[0] == 'd') {
    if (key[0] >= 'A' && key[0] <= 'Z' && key[1] == '\0') {
      if (shift) return key[0];
      else return key[0] - 'A' + 'a';
    }
    for (auto item: SHIFT) {
      if (strcmp(item.name, key) == 0) {
        if (shift) return item.shift;
        else return item.noshift;
      }
    }
  }
  return '\0';
}

char handle_key(SDL_Event *ev) {
  static int shift = 0;
  int key = ev->key.keysym.sym;
  if (key == SDLK_LSHIFT || key == SDLK_RSHIFT) { shift ^= 1; return '\0'; }

  if (ev->type == SDL_KEYDOWN) {
    for (auto item: SHIFT) {
      if (item.keycode == key) {
        if (shift) return item.shift;
        else return item.noshift;
      }
    }
  }
  return '\0';
}
