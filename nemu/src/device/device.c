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
#include <stdarg.h>
#include <errno.h>
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
void init_xv6_e1000();
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
static bool xv6_stdin_raw_mode = false;
static bool xv6_stdin_need_close = false;
static bool xv6_stdin_trace = false;
static int xv6_stdin_fd = STDIN_FILENO;
static int xv6_stdin_flags = -1;
static int xv6_trace_fd = -1;
static struct termios xv6_stdin_termios = {};
static const char *xv6_stdin_source = "stdin";

static bool env_enabled(const char *name) {
  const char *value = getenv(name);
  return value != NULL &&
    (!strcmp(value, "1") || !strcmp(value, "true") || !strcmp(value, "yes"));
}

static void trace_open_file_once(void) {
  if (xv6_trace_fd >= 0) return;
  const char *path = getenv("NEMU_XV6_UART_TRACE_FILE");
  if (path == NULL || *path == '\0') return;
  xv6_trace_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

static void trace_printf(const char *fmt, ...) {
  if (!xv6_stdin_trace) return;
  trace_open_file_once();

  va_list ap;
  va_start(ap, fmt);
  if (xv6_trace_fd >= 0) {
    vdprintf(xv6_trace_fd, fmt, ap);
    dprintf(xv6_trace_fd, "\n");
  } else {
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
  }
  va_end(ap);
}

static void trace_xv6_uart_bytes(const uint8_t *buf, ssize_t nread) {
  if (!xv6_stdin_trace || nread <= 0) return;

  char hexbuf[3 * 16 + 1] = {};
  int off = 0;
  ssize_t limit = nread < 16 ? nread : 16;
  for (ssize_t i = 0; i < limit && off + 3 < (int)sizeof(hexbuf); i++) {
    off += snprintf(hexbuf + off, sizeof(hexbuf) - off, "%02x", buf[i]);
    if (i + 1 < limit && off + 1 < (int)sizeof(hexbuf)) {
      hexbuf[off++] = ' ';
      hexbuf[off] = '\0';
    }
  }

  trace_printf("[device] input fd=%d nread=%d bytes=[%s]%s",
      xv6_stdin_fd, (int)nread, hexbuf, (nread > limit ? " ..." : ""));
}

static void restore_xv6_uart_stdin(void) {
#ifdef CONFIG_HAS_XV6_UART
  if (!xv6_stdin_ready) return;
  if (xv6_stdin_is_tty && xv6_stdin_raw_mode) {
    tcsetattr(xv6_stdin_fd, TCSAFLUSH, &xv6_stdin_termios);
  }
  if (xv6_stdin_flags >= 0) {
    fcntl(xv6_stdin_fd, F_SETFL, xv6_stdin_flags);
  }
  if (xv6_stdin_need_close) {
    close(xv6_stdin_fd);
  }
#endif
}

static void init_xv6_uart_stdin(void) {
#ifdef CONFIG_HAS_XV6_UART
  const char *stdin_mode = getenv("NEMU_XV6_UART_INPUT");
  bool prefer_stdin =
    (stdin_mode != NULL) &&
    (!strcmp(stdin_mode, "stdin") || !strcmp(stdin_mode, "pipe"));
  xv6_stdin_trace = env_enabled("NEMU_XV6_UART_TRACE");

  if (!prefer_stdin) {
    xv6_stdin_fd = open("/dev/tty", O_RDONLY);
  } else {
    xv6_stdin_fd = -1;
  }

  if (xv6_stdin_fd >= 0) {
    xv6_stdin_need_close = true;
    xv6_stdin_source = "/dev/tty";
  } else {
    xv6_stdin_fd = STDIN_FILENO;
    xv6_stdin_need_close = false;
    xv6_stdin_source = prefer_stdin ? "stdin" : "stdin-fallback";
  }

  xv6_stdin_flags = fcntl(xv6_stdin_fd, F_GETFL, 0);
  if (xv6_stdin_flags < 0) {
    trace_printf("[device] init failed F_GETFL fd=%d errno=%d", xv6_stdin_fd, errno);
    return;
  }

  xv6_stdin_is_tty = isatty(xv6_stdin_fd);
  if (xv6_stdin_is_tty) {
    xv6_stdin_raw_mode = env_enabled("NEMU_XV6_UART_RAW");

    if (xv6_stdin_raw_mode) {
      if (tcgetattr(xv6_stdin_fd, &xv6_stdin_termios) != 0) return;
      struct termios raw = xv6_stdin_termios;
      raw.c_lflag &= ~(ICANON | ECHO);
      raw.c_iflag &= ~(IXON | ICRNL);
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      if (tcsetattr(xv6_stdin_fd, TCSAFLUSH, &raw) != 0) return;
    }
  }

  if (fcntl(xv6_stdin_fd, F_SETFL, xv6_stdin_flags | O_NONBLOCK) != 0) {
    if (xv6_stdin_is_tty && xv6_stdin_raw_mode) tcsetattr(xv6_stdin_fd, TCSAFLUSH, &xv6_stdin_termios);
    return;
  }
  atexit(restore_xv6_uart_stdin);
  xv6_stdin_ready = true;
  trace_printf("[device] input ready source=%s fd=%d tty=%d raw=%d trace=%d",
      xv6_stdin_source,
      xv6_stdin_fd, xv6_stdin_is_tty, xv6_stdin_raw_mode, xv6_stdin_trace);
#endif
}

static void poll_xv6_uart_stdin() {
#ifdef CONFIG_HAS_XV6_UART
  if (!xv6_stdin_ready) return;
  fd_set rfds;
  struct timeval timeout = {};
  FD_ZERO(&rfds);
  FD_SET(xv6_stdin_fd, &rfds);
  int ret = select(xv6_stdin_fd + 1, &rfds, NULL, NULL, &timeout);
  if (ret <= 0 || !FD_ISSET(xv6_stdin_fd, &rfds)) return;

  uint8_t buf[128];
  ssize_t nread = read(xv6_stdin_fd, buf, sizeof(buf));
  if (nread <= 0) {
    if (xv6_stdin_trace && nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      trace_printf("[device] read error fd=%d errno=%d", xv6_stdin_fd, errno);
    }
    return;
  }
  trace_xv6_uart_bytes(buf, nread);
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
  IFDEF(CONFIG_HAS_XV6_E1000, init_xv6_e1000());
  IFDEF(CONFIG_HAS_TIMER, init_timer());
  IFDEF(CONFIG_HAS_VGA, init_vga());
  IFDEF(CONFIG_HAS_KEYBOARD, init_i8042());
  IFDEF(CONFIG_HAS_AUDIO, init_audio());
  IFDEF(CONFIG_HAS_DISK, init_disk());
  IFDEF(CONFIG_HAS_SDCARD, init_sdcard());

  IFNDEF(CONFIG_TARGET_AM, init_alarm());
}
