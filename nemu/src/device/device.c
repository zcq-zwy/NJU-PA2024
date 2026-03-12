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
#include <utils.h>
#include <device/alarm.h>
#ifndef CONFIG_TARGET_AM
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

void init_map();
void init_serial();
void init_xv6_uart();
void init_xv6_clint();
void init_xv6_plic();
void init_xv6_virtio_blk();
void init_timer();
void init_vga();
void init_i8042();
void init_audio();
void init_disk();
void init_sdcard();
void init_alarm();

void send_key(uint8_t, bool);
void vga_update_screen();
void xv6_clint_update();
void xv6_uart_input_char(uint8_t ch);

#ifndef CONFIG_TARGET_AM
static bool xv6_stdin_ready = false;
static bool xv6_stdin_is_tty = false;
static int xv6_stdin_flags = -1;
static struct termios xv6_stdin_termios = {};

static void restore_xv6_uart_stdin(void) {
#ifdef CONFIG_HAS_XV6_UART
  if (!xv6_stdin_ready) return;
  if (xv6_stdin_is_tty) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &xv6_stdin_termios);
  }
  if (xv6_stdin_flags >= 0) {
    fcntl(STDIN_FILENO, F_SETFL, xv6_stdin_flags);
  }
#endif
}

static void init_xv6_uart_stdin(void) {
#ifdef CONFIG_HAS_XV6_UART
  xv6_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (xv6_stdin_flags < 0) return;

  xv6_stdin_is_tty = isatty(STDIN_FILENO);
  if (xv6_stdin_is_tty) {
    if (tcgetattr(STDIN_FILENO, &xv6_stdin_termios) != 0) return;
    struct termios raw = xv6_stdin_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return;
  }

  if (fcntl(STDIN_FILENO, F_SETFL, xv6_stdin_flags | O_NONBLOCK) != 0) {
    if (xv6_stdin_is_tty) tcsetattr(STDIN_FILENO, TCSAFLUSH, &xv6_stdin_termios);
    return;
  }
  atexit(restore_xv6_uart_stdin);
  xv6_stdin_ready = true;
#endif
}

static void poll_xv6_uart_stdin() {
#ifdef CONFIG_HAS_XV6_UART
  if (!xv6_stdin_ready) return;
  fd_set rfds;
  struct timeval timeout = {};
  FD_ZERO(&rfds);
  FD_SET(STDIN_FILENO, &rfds);
  int ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &timeout);
  if (ret <= 0 || !FD_ISSET(STDIN_FILENO, &rfds)) return;

  uint8_t buf[128];
  ssize_t nread = read(STDIN_FILENO, buf, sizeof(buf));
  if (nread <= 0) return;
  for (ssize_t i = 0; i < nread; i++) {
    xv6_uart_input_char(buf[i]);
  }
#endif
}
#endif

void device_update() {
  static uint64_t last = 0;
  uint64_t now = get_time();
  if (now - last < 1000000 / TIMER_HZ) {
    return;
  }
  last = now;

  IFDEF(CONFIG_HAS_VGA, vga_update_screen());
  IFDEF(CONFIG_HAS_XV6_CLINT, xv6_clint_update());
  IFNDEF(CONFIG_TARGET_AM, poll_xv6_uart_stdin());

#ifndef CONFIG_TARGET_AM
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        nemu_state.state = NEMU_QUIT;
        break;
#ifdef CONFIG_HAS_KEYBOARD
      // If a key was pressed
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        uint8_t k = event.key.keysym.scancode;
        bool is_keydown = (event.key.type == SDL_KEYDOWN);
        send_key(k, is_keydown);
        break;
      }
#endif
      default: break;
    }
  }
#endif
}

void sdl_clear_event_queue() {
#ifndef CONFIG_TARGET_AM
  SDL_Event event;
  while (SDL_PollEvent(&event));
#endif
}

void init_device() {
  IFDEF(CONFIG_TARGET_AM, ioe_init());
  init_map();

#ifndef CONFIG_TARGET_AM
  IFDEF(CONFIG_HAS_XV6_UART, init_xv6_uart_stdin());
#endif

  IFDEF(CONFIG_HAS_SERIAL, init_serial());
  IFDEF(CONFIG_HAS_XV6_UART, init_xv6_uart());
  IFDEF(CONFIG_HAS_XV6_CLINT, init_xv6_clint());
  IFDEF(CONFIG_HAS_XV6_PLIC, init_xv6_plic());
  IFDEF(CONFIG_HAS_XV6_VIRTIO_BLK, init_xv6_virtio_blk());
  IFDEF(CONFIG_HAS_TIMER, init_timer());
  IFDEF(CONFIG_HAS_VGA, init_vga());
  IFDEF(CONFIG_HAS_KEYBOARD, init_i8042());
  IFDEF(CONFIG_HAS_AUDIO, init_audio());
  IFDEF(CONFIG_HAS_DISK, init_disk());
  IFDEF(CONFIG_HAS_SDCARD, init_sdcard());

  IFNDEF(CONFIG_TARGET_AM, init_alarm());
}
