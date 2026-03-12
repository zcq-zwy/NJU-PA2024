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

#include <utils.h>
#include <device/map.h>

enum {
  UART_RHR = 0,
  UART_THR = 0,
  UART_IER = 1,
  UART_FCR = 2,
  UART_ISR = 2,
  UART_LCR = 3,
  UART_LSR = 5,
  UART_REG_NR = 8,
};

static uint8_t *xv6_uart_base = NULL;

static void xv6_uart_putc(uint8_t ch) {
  putc(ch, stderr);
}

static void update_lsr(void) {
  xv6_uart_base[UART_LSR] = (1 << 5) | (1 << 6);
}

static void xv6_uart_io_handler(uint32_t offset, int len, bool is_write) {
  assert(len == 1);
  assert(offset < UART_REG_NR);

  switch (offset) {
    case UART_THR:
      if (is_write) {
        xv6_uart_putc(xv6_uart_base[UART_THR]);
        update_lsr();
      } else {
        xv6_uart_base[UART_RHR] = 0xff;
      }
      break;
    case UART_IER:
    case UART_LCR:
      break;
    case UART_FCR:
      if (!is_write) {
        xv6_uart_base[UART_ISR] = 0x01;
      }
      break;
    case UART_LSR:
      if (!is_write) {
        update_lsr();
      }
      break;
    default:
      break;
  }
}

void init_xv6_uart() {
  xv6_uart_base = new_space(UART_REG_NR);
  memset(xv6_uart_base, 0, UART_REG_NR);
  xv6_uart_base[UART_ISR] = 0x01;
  update_lsr();
  add_mmio_map("xv6-uart", CONFIG_XV6_UART_MMIO, xv6_uart_base, UART_REG_NR, xv6_uart_io_handler);
}
