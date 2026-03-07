#include "ppu.h"
#include "cpu.h"
#include "fce.h"
#include "memory.h"
#include <klib.h>

//#define PROFILE
//#define HAS_US_TIMER

PPU_STATE ppu;

static bool ppu_2007_first_read;
static byte ppu_addr_latch;
static byte PPU_SPRRAM[0x100];
static byte PPU_RAM[0x4000];
static uint32_t PPU_PALETTE_RGB[0x20];
static bool ppu_sprite_hit_occured = false;
static byte ppu_latch;

// PPU Constants
static const word ppu_base_nametable_addresses[4] = { 0x2000, 0x2400, 0x2800, 0x2C00 };

// For sprite-0-hit checks
static byte ppu_screen_background[248][264];

// Precalculated tile high and low bytes addition for pattern tables
static byte ppu_l_h_addition_table[256][256][8];
static byte ppu_l_h_addition_flip_table[256][256][8];


// PPUCTRL Functions

word ppu_base_nametable_address()           { return ppu_base_nametable_addresses[ppu.PPUCTRL & 0x3];  }
byte ppu_vram_address_increment()           { return common_bit_set(ppu.PPUCTRL, 2) ? 32 : 1;          }
word ppu_sprite_pattern_table_address()     { return common_bit_set(ppu.PPUCTRL, 3) ? 0x1000 : 0x0000; }
word ppu_background_pattern_table_address() { return common_bit_set(ppu.PPUCTRL, 4) ? 0x1000 : 0x0000; }
byte ppu_sprite_height()                    { return common_bit_set(ppu.PPUCTRL, 5) ? 16 : 8;          }
bool ppu_generates_nmi()                    { return common_bit_set(ppu.PPUCTRL, 7);                   }

// PPUMASK Functions

bool ppu_renders_grayscale()                { return common_bit_set(ppu.PPUMASK, 0); }
bool ppu_shows_background_in_leftmost_8px() { return common_bit_set(ppu.PPUMASK, 1); }
bool ppu_shows_sprites_in_leftmost_8px()    { return common_bit_set(ppu.PPUMASK, 2); }
bool ppu_shows_background()                 { return common_bit_set(ppu.PPUMASK, 3); }
bool ppu_shows_sprites()                    { return common_bit_set(ppu.PPUMASK, 4); }
bool ppu_intensifies_reds()                 { return common_bit_set(ppu.PPUMASK, 5); }
bool ppu_intensifies_greens()               { return common_bit_set(ppu.PPUMASK, 6); }
bool ppu_intensifies_blues()                { return common_bit_set(ppu.PPUMASK, 7); }

void ppu_set_renders_grayscale(bool yesno)                { common_modify_bitb(&ppu.PPUMASK, 0, yesno); }
void ppu_set_shows_background_in_leftmost_8px(bool yesno) { common_modify_bitb(&ppu.PPUMASK, 1, yesno); }
void ppu_set_shows_sprites_in_leftmost_8px(bool yesno)    { common_modify_bitb(&ppu.PPUMASK, 2, yesno); }
void ppu_set_shows_background(bool yesno)                 { common_modify_bitb(&ppu.PPUMASK, 3, yesno); }
void ppu_set_shows_sprites(bool yesno)                    { common_modify_bitb(&ppu.PPUMASK, 4, yesno); }
void ppu_set_intensifies_reds(bool yesno)                 { common_modify_bitb(&ppu.PPUMASK, 5, yesno); }
void ppu_set_intensifies_greens(bool yesno)               { common_modify_bitb(&ppu.PPUMASK, 6, yesno); }
void ppu_set_intensifies_blues(bool yesno)                { common_modify_bitb(&ppu.PPUMASK, 7, yesno); }

// PPUSTATUS Functions

bool ppu_sprite_overflow()               { return common_bit_set(ppu.PPUSTATUS, 5); }
bool ppu_sprite_0_hit()                  { return common_bit_set(ppu.PPUSTATUS, 6); }
bool ppu_in_vblank()                     { return common_bit_set(ppu.PPUSTATUS, 7); }

void ppu_set_sprite_overflow(bool yesno) { common_modify_bitb(&ppu.PPUSTATUS, 5, yesno); }
void ppu_set_sprite_0_hit(bool yesno)    { common_modify_bitb(&ppu.PPUSTATUS, 6, yesno); }
void ppu_set_in_vblank(bool yesno)       { common_modify_bitb(&ppu.PPUSTATUS, 7, yesno); }


// RAM

word ppu_get_real_ram_address(word address) {
  if (address < 0x2000) { return address; }
  else if (address < 0x3F00) {
    if (address < 0x3000) { return address; }
    else { return address; }
  }
  else if (address < 0x4000) {
    address = 0x3F00 | (address & 0x1F);
    if (address == 0x3F10 || address == 0x3F14 || address == 0x3F18 || address == 0x3F1C)
      address -= 0x10;
    return address;
  }
  return 0xFFFF;
}

byte ppu_ram_read(word address) {
  return PPU_RAM[ppu_get_real_ram_address(address)];
}

void ppu_ram_write(word address, byte data) {
  word real = ppu_get_real_ram_address(address);
  PPU_RAM[real] = data;
  if ((real & 0x3F00) == 0x3F00) {
    PPU_PALETTE_RGB[real & 0x1F] = palette[data & 0x3F];
  }
}

// 3F01 = 0F (00001111)
// 3F02 = 2A (00101010)
// 3F03 = 09 (00001001)
// 3F04 = 07 (00000111)
// 3F05 = 0F (00001111)
// 3F06 = 30 (00110000)
// 3F07 = 27 (00100111)
// 3F08 = 15 (00010101)
// 3F09 = 0F (00001111)
// 3F0A = 30 (00110000)
// 3F0B = 02 (00000010)
// 3F0C = 21 (00100001)
// 3F0D = 0F (00001111)
// 3F0E = 30 (00110000)
// 3F0F = 00 (00000000)
// 3F11 = 0F (00001111)
// 3F12 = 16 (00010110)
// 3F13 = 12 (00010010)
// 3F14 = 37 (00110111)
// 3F15 = 0F (00001111)
// 3F16 = 12 (00010010)
// 3F17 = 16 (00010110)
// 3F18 = 37 (00110111)
// 3F19 = 0F (00001111)
// 3F1A = 17 (00010111)
// 3F1B = 11 (00010001)
// 3F1C = 35 (00110101)
// 3F1D = 0F (00001111)
// 3F1E = 17 (00010111)
// 3F1F = 11 (00010001)
// 3F20 = 2B (00101011)


// Rendering

void ppu_draw_background_scanline(bool mirror) {
  int scanline = ppu.scanline;
  int tile_y = scanline >> 3;
  int y_in_tile = scanline & 0x7;
  int screen_off_x = mirror ? 256 : 0;
  int scroll_x = ppu.PPUSCROLL_X;
  int y_draw = scanline + 1;
  bool render_scanline = fce_draw_enabled && ((unsigned)y_draw < SCR_H);
#if AGGRESSIVE_PPU_HALF_RES
  if (render_scanline && (y_draw & 1)) render_scanline = false;
#endif
  bool do_draw = render_scanline;
  uint32_t *canvas_row = do_draw ? &fce_canvas[y_draw * SCR_W] : NULL;

  byte sprite0_x = PPU_SPRRAM[3];
  byte sprite0_y = PPU_SPRRAM[0];
  int sprite0_h = ppu_sprite_height();
  bool need_hit_row = !ppu_sprite_hit_occured && ppu_shows_background() &&
      sprite0_y <= scanline && sprite0_y + sprite0_h >= scanline;
  int hit_x_begin = sprite0_x;
  int hit_x_end = sprite0_x + 8;
  byte *bg_hit_row = need_hit_row ? ppu_screen_background[scanline] : NULL;

  if (!do_draw && !need_hit_row) {
    return;
  }

  word nametable_base = ppu_base_nametable_address() + (mirror ? 0x400 : 0);
  word attribute_base = nametable_base + 0x3C0 + ((scanline >> 5) * 8);
  word pattern_base = ppu_background_pattern_table_address();

  bool top = (scanline & 31) < 16;
  int tile_x_start = ppu_shows_background_in_leftmost_8px() ? 0 : 1;
  int vis_start = scroll_x - screen_off_x - 7;
  int vis_end = scroll_x - screen_off_x + (SCR_W - 1);
  int tile_x_begin = vis_start > 0 ? ((vis_start + 7) >> 3) : 0;
  int tile_x_end = vis_end >= 0 ? (vis_end >> 3) : -1;
  if (!do_draw && need_hit_row) {
    tile_x_begin = hit_x_begin >> 3;
    tile_x_end = (hit_x_end - 1) >> 3;
  }
  if (tile_x_begin < tile_x_start) tile_x_begin = tile_x_start;
  if (tile_x_end > 31) tile_x_end = 31;
  if (tile_x_begin > tile_x_end) return;

  for (int tile_x = tile_x_begin; tile_x <= tile_x_end; tile_x++) {
    int screen_x0 = (tile_x << 3) - scroll_x + screen_off_x;

    int tile_index = PPU_RAM[nametable_base + tile_x + (tile_y << 5)];
    word tile_address = pattern_base + (tile_index << 4);
    byte l = PPU_RAM[tile_address + y_in_tile];
    byte h = PPU_RAM[tile_address + y_in_tile + 8];

    byte palette_attribute = PPU_RAM[attribute_base + (tile_x >> 2)];
    if (!top) palette_attribute >>= 4;
    if ((tile_x & 3) >= 2) palette_attribute >>= 2;
    const uint32_t *palette_rgb = &PPU_PALETTE_RGB[((palette_attribute & 3) << 2)];

    const byte *lh = ppu_l_h_addition_table[l][h];
    int x_begin = 0;
    int x_end = 8;
    if (do_draw) {
      if (screen_x0 < 0) x_begin = -screen_x0;
      if (screen_x0 + 8 > SCR_W) x_end = SCR_W - screen_x0;
    }
    else {
      int tile_x0 = tile_x << 3;
      if (tile_x0 < hit_x_begin) x_begin = hit_x_begin - tile_x0;
      if (tile_x0 + 8 > hit_x_end) x_end = hit_x_end - tile_x0;
    }

    for (int x = x_begin; x < x_end; x++) {
      byte color = lh[x];
      if (color != 0) {
        int bgx = (tile_x << 3) + x;
        int screen_x = screen_x0 + x;
        if (need_hit_row && bgx >= hit_x_begin && bgx < hit_x_end) {
          bg_hit_row[bgx] = color;
        }
        if (do_draw) {
          canvas_row[screen_x] = palette_rgb[color];
        }
      }
    }
  }
}

void ppu_draw_sprite_scanline() {
  int scanline = ppu.scanline;
  int sprite_h = ppu_sprite_height();
  word sprite_pattern_base = ppu_sprite_pattern_table_address();
  bool show_bg = ppu_shows_background();
  int y_draw = scanline + 1;
  bool render_scanline = fce_draw_enabled && ((unsigned)y_draw < SCR_H);
#if AGGRESSIVE_PPU_HALF_RES
  if (render_scanline && (y_draw & 1)) render_scanline = false;
#endif

  if (!render_scanline) {
    if (!show_bg || ppu_sprite_hit_occured) return;
    byte sprite0_y = PPU_SPRRAM[0];
    if (sprite0_y > scanline || sprite0_y + sprite_h < scanline) return;
  }

  int scanline_sprite_count = 0;
  int sprite_end = render_scanline ? 0x100 : 4;
  for (int n = 0; n < sprite_end; n += 4) {
    byte sprite_x = PPU_SPRRAM[n + 3];
    byte sprite_y = PPU_SPRRAM[n];

    if (sprite_y > scanline || sprite_y + sprite_h < scanline) continue;

    scanline_sprite_count++;
    if (scanline_sprite_count > 8) {
      ppu_set_sprite_overflow(true);
      break;
    }

    byte sprite_attr = PPU_SPRRAM[n + 2];
    bool vflip = sprite_attr & 0x80;
    bool hflip = sprite_attr & 0x40;

    word tile_address = sprite_pattern_base + 16 * PPU_SPRRAM[n + 1];
    int y_in_tile = scanline & 0x7;
    int y_fetch = vflip ? (7 - y_in_tile) : y_in_tile;
    byte l = PPU_RAM[tile_address + y_fetch];
    byte h = PPU_RAM[tile_address + y_fetch + 8];

    const uint32_t *palette_rgb = &PPU_PALETTE_RGB[0x10 + ((sprite_attr & 0x3) << 2)];
    const byte *lh = hflip ? ppu_l_h_addition_flip_table[l][h] : ppu_l_h_addition_table[l][h];

    bool do_draw = render_scanline && ((unsigned)y_draw < SCR_H);
    uint32_t *canvas_row = do_draw ? &fce_canvas[y_draw * SCR_W] : NULL;

    int x_end = 8;
    if (sprite_x + 8 > SCR_W) x_end = SCR_W - sprite_x;
    if (x_end <= 0) continue;

    bool check_hit = show_bg && !ppu_sprite_hit_occured && (n == 0);
    byte *bg_hit_row = check_hit ? ppu_screen_background[sprite_y + y_in_tile] : NULL;
    for (int x = 0; x < x_end; x++) {
      int color = lh[x];
      if (color != 0) {
        int screen_x = sprite_x + x;
        if (do_draw) {
          canvas_row[screen_x] = palette_rgb[color];
        }
        if (check_hit && bg_hit_row[screen_x] == color) {
          ppu_set_sprite_0_hit(true);
          ppu_sprite_hit_occured = true;
          check_hit = false;
        }
      }
    }
  }
}

// PPU Lifecycle

void ppu_run(int cycles) {
  while (cycles-- > 0) { ppu_cycle(); }
}

static uint32_t background_time, sprite_time, cpu_time;
#ifdef PROFILE
#ifdef HAS_US_TIMER
# define TIMER_UNIT "us"
# define time_read(x) read_us(&x)
# define time_diff(t1, t0) us_timediff(&t1, &t0)
# define TIME_TYPE amtime
#else
# define TIMER_UNIT "ms"
# define time_read(x) x = uptime()
# define time_diff(t1, t0) (t1 - t0)
# define TIME_TYPE uint32_t
#endif
#else
# define time_read(x)
# define time_diff(t1, t0) 0
#endif

void ppu_cycle() {
#ifdef PROFILE
  TIME_TYPE t0, t1, t2, t3;
#endif

  if (!ppu.ready && cpu_clock() > 29658)
    ppu.ready = true;

  ppu.scanline++;

  time_read(t0);
  if (ppu.scanline < SCR_H && ppu_shows_background()) {
    ppu_draw_background_scanline(false);
    ppu_draw_background_scanline(true);
  }

  if (ppu.scanline < SCR_H && ppu_shows_sprites()) {
    ppu_draw_sprite_scanline();
  }
  time_read(t1);

  cpu_run(341);
  time_read(t2);

  background_time += time_diff(t1, t0);
  cpu_time += time_diff(t2, t1);

  if (ppu.scanline == 241) {
    ppu_set_in_vblank(true);
    ppu_set_sprite_0_hit(false);
    cpu_interrupt();
  }
  else if (ppu.scanline == 262) {
    ppu.scanline = -1;
    ppu_sprite_hit_occured = false;
    ppu_set_in_vblank(false);

    time_read(t3);
    fce_update_screen();
    time_read(t0);

#ifdef PROFILE
    uint32_t total = cpu_time + background_time + sprite_time + time_diff(t0, t3);
    printf("Time: cpu + bg + spr + scr = (%d + %d + %d + %d)\t= %d %s\n",
        cpu_time, background_time, sprite_time, time_diff(t0, t3), total, TIMER_UNIT);
#endif
    cpu_time = 0;
    background_time = 0;
    sprite_time = 0;
  }
}

void ppu_copy(word address, byte *source, int length) {
  memcpy(&PPU_RAM[address], source, length);
}

byte ppuio_read(word address) {
  ppu.PPUADDR &= 0x3FFF;
  switch (address & 7) {
    case 2:
      {
        byte value = ppu.PPUSTATUS;
        ppu_set_in_vblank(false);
        ppu_set_sprite_0_hit(false);
        ppu.scroll_received_x = 0;
        ppu.PPUSCROLL = 0;
        ppu.addr_received_high_byte = 0;
        ppu_latch = value;
        ppu_addr_latch = 0;
        ppu_2007_first_read = true;
        return value;
      }
    case 4: return ppu_latch = PPU_SPRRAM[ppu.OAMADDR];
    case 7:
      {
        byte data;

        if (ppu.PPUADDR < 0x3F00) {
          data = ppu_latch = ppu_ram_read(ppu.PPUADDR);
        }
        else {
          data = ppu_ram_read(ppu.PPUADDR);
          ppu_latch = 0;
        }

        if (ppu_2007_first_read) {
          ppu_2007_first_read = false;
        }
        else {
          ppu.PPUADDR += ppu_vram_address_increment();
        }
        return data;
      }
    default: return 0xFF;
  }
}

void ppuio_write(word address, byte data) {
  address &= 7;
  ppu_latch = data;
  ppu.PPUADDR &= 0x3FFF;
  switch(address) {
    case 0: if (ppu.ready) ppu.PPUCTRL = data; break;
    case 1: if (ppu.ready) ppu.PPUMASK = data; break;
    case 3: ppu.OAMADDR = data; break;
    case 4: PPU_SPRRAM[ppu.OAMADDR++] = data; break;
    case 5:
            {
              if (ppu.scroll_received_x)
                ppu.PPUSCROLL_Y = data;
              else
                ppu.PPUSCROLL_X = data;

              ppu.scroll_received_x ^= 1;
              break;
            }
    case 6:
            {
              if (!ppu.ready)
                return;

              if (ppu.addr_received_high_byte)
                ppu.PPUADDR = (ppu_addr_latch << 8) + data;
              else
                ppu_addr_latch = data;

              ppu.addr_received_high_byte ^= 1;
              ppu_2007_first_read = true;
              break;
            }
    case 7:
            {
              if (ppu.PPUADDR > 0x1FFF || ppu.PPUADDR < 0x4000) {
                ppu_ram_write(ppu.PPUADDR ^ ppu.mirroring_xor, data);
                ppu_ram_write(ppu.PPUADDR, data);
              }
              else {
                ppu_ram_write(ppu.PPUADDR, data);
              }
            }
  }
  ppu_latch = data;
}

void ppu_init() {
  ppu.PPUCTRL = ppu.PPUMASK = ppu.PPUSTATUS = ppu.OAMADDR = ppu.PPUSCROLL_X = ppu.PPUSCROLL_Y = ppu.PPUADDR = 0;
  ppu.PPUSTATUS |= 0xA0;
  ppu.PPUDATA = 0;
  ppu_2007_first_read = true;
  for (int i = 0; i < 0x20; i++) {
    PPU_PALETTE_RGB[i] = palette[0];
  }

  // Initializing low-high byte-pairs for pattern tables
  int h, l, x;
  for (h = 0; h < 0x100; h ++) {
    for (l = 0; l < 0x100; l ++) {
      for (x = 0; x < 8; x ++) {
        ppu_l_h_addition_table[l][h][x] = (((h >> (7 - x)) & 1) << 1) | ((l >> (7 - x)) & 1);
        ppu_l_h_addition_flip_table[l][h][x] = (((h >> x) & 1) << 1) | ((l >> x) & 1);
      }
    }
  }
}

void ppu_sprram_write(byte data) {
  PPU_SPRRAM[ppu.OAMADDR++] = data;
}

void ppu_set_background_color(byte color) {
}

void ppu_set_mirroring(byte mirroring) {
  ppu.mirroring = mirroring;
  ppu.mirroring_xor = 0x400 << mirroring;
}
