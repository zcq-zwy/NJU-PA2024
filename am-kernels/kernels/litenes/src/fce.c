#include "fce.h"
#include "cpu.h"
#include "memory.h"
#include "ppu.h"
#include "psg.h"
#include <klib.h>

static int frame_cnt;
static int emu_fps_cnt;
static int display_fps_cnt;
static unsigned long long total_emu_frames;
static unsigned long long total_display_frames;
static int fps_stats_start_ms;
bool fce_draw_enabled = true;

static int xpad_cached, ypad_cached;
static bool screen_pos_inited;

uint32_t fce_canvas[SCR_W * SCR_H];

typedef struct {
  char signature[4];
  byte prg_block_count;
  byte chr_block_count;
  word rom_type;
  byte reserved[8];
} ines_header;

static byte *buf;
static ines_header *fce_rom_header;

byte *romread(int size) {
  byte *ret = buf;
  buf += size;
  return ret;
}

int fce_load_rom(char *rom) {
  buf = (byte*)rom;
  fce_rom_header = (ines_header*)romread(sizeof(ines_header));

  if (memcmp(fce_rom_header->signature, "NES\x1A", 4)) {
    return -1;
  }

  mmc_id = ((fce_rom_header->rom_type & 0xF0) >> 4);

  int prg_size = fce_rom_header->prg_block_count * 0x4000;

  byte *blk = romread(prg_size);

  if (mmc_id == 0 || mmc_id == 3) {
    // if there is only one PRG block, we must repeat it twice
    if (fce_rom_header->prg_block_count == 1) {
      mmc_copy(0x8000, blk, 0x4000);
      mmc_copy(0xC000, blk, 0x4000);
    }
    else {
      mmc_copy(0x8000, blk, 0x8000);
    }
  }
  else {
    return -1;
  }

  // Copying CHR pages into MMC and PPU
  int i;
  for (i = 0; i < fce_rom_header->chr_block_count; i++) {
    byte *chr = romread(0x2000);
    mmc_append_chr_rom_page(chr);

    if (i == 0) {
      ppu_copy(0x0000, chr, 0x2000);
    }
  }

  return 0;
}

void fce_init() {
  cpu_init();
  ppu_init();
  psg_init();
  ppu_set_mirroring(fce_rom_header->rom_type & 1);
  cpu_reset();

  frame_cnt = 0;
  emu_fps_cnt = 0;
  display_fps_cnt = 0;
  total_emu_frames = 0;
  total_display_frames = 0;
  fps_stats_start_ms = 0;
  fce_draw_enabled = true;
  screen_pos_inited = false;
}

static int gtime;

static inline int uptime_ms() {
  return io_read(AM_TIMER_UPTIME).us / 1000;
}

void wait_for_frame() {
  int cur = uptime_ms();
  while (cur - gtime < 1000 / FPS) {
    cur = uptime_ms();
  }
  gtime = cur;
}

void fce_print_fps_summary() {
  if (fps_stats_start_ms == 0) return;

  int upt = uptime_ms();
  int elapsed = upt - fps_stats_start_ms;
  if (elapsed <= 0) elapsed = 1;
  unsigned long long avg_emu_fps_x100 = total_emu_frames * 100000ULL / (unsigned)elapsed;
  unsigned long long avg_display_fps_x100 = total_display_frames * 100000ULL / (unsigned)elapsed;

  putch('\n');
  printf("FPS summary: AVG FPS = %llu.%02llu  AVG EMU FPS = %llu.%02llu  AVG DISPLAY FPS = %llu.%02llu\n",
      avg_emu_fps_x100 / 100, avg_emu_fps_x100 % 100,
      avg_emu_fps_x100 / 100, avg_emu_fps_x100 % 100,
      avg_display_fps_x100 / 100, avg_display_fps_x100 % 100);
}

// FCE Lifecycle

void fce_run() {
  gtime = uptime_ms();
  fps_stats_start_ms = gtime;
  uint32_t last = gtime;
  while(1) {
    wait_for_frame();

    // Poll keyboard once per frame instead of once per scanline.
    psg_detect_key();

    int scanlines = 262;
    while (scanlines-- > 0) {
      ppu_cycle();
    }

    emu_fps_cnt++;
    total_emu_frames++;
    int upt = uptime_ms();
    if (upt - last > 1000) {
      last = upt;
      for (int i = 0; i < 96; i++) putch('\b');
      printf("(System time: %ds) FPS = %d  EMU FPS = %d  DISPLAY FPS = %d",
          upt / 1000, emu_fps_cnt, emu_fps_cnt, display_fps_cnt);
      emu_fps_cnt = 0;
      display_fps_cnt = 0;
    }
  }
}

void fce_update_screen() {
  if (!fce_draw_enabled) {
    frame_cnt++;
    fce_draw_enabled = (frame_cnt % (1 + FRAME_SKIP) == 0);
    return;
  }

  int idx = ppu_ram_read(0x3F00);
  uint32_t bgc = palette[idx];

  if (!screen_pos_inited) {
    AM_GPU_CONFIG_T cfg = io_read(AM_GPU_CONFIG);
    xpad_cached = (cfg.width  - SCR_W) / 2;
    ypad_cached = (cfg.height - SCR_H) / 2;
    panic_on(xpad_cached < 0 || ypad_cached < 0, "screen too small");
    screen_pos_inited = true;
  }

#if AGGRESSIVE_PPU_HALF_RES
  for (int y = 1; y < SCR_H; y += 2) {
    memcpy(&fce_canvas[y * SCR_W], &fce_canvas[(y - 1) * SCR_W], SCR_W * sizeof(uint32_t));
  }
#endif

  io_write(AM_GPU_FBDRAW, xpad_cached, ypad_cached, fce_canvas, SCR_W, SCR_H, true);
  display_fps_cnt++;
  total_display_frames++;

  for (int i = 0; i < SCR_W * SCR_H; i ++) fce_canvas[i] = bgc;

  frame_cnt++;
  fce_draw_enabled = (frame_cnt % (1 + FRAME_SKIP) == 0);
}
