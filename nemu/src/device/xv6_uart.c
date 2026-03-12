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

#define UART_LSR_RX_READY      (1 << 0)
#define UART_LSR_TX_EMPTY      (1 << 5)
#define UART_LSR_TX_IDLE       (1 << 6)
#define UART_IER_RX_ENABLE     (1 << 0)
#define UART_IIR_NO_INT        0x01
#define UART_IIR_RX_AVAILABLE  0x04
#define UART_QUEUE_LEN         1024

static uint8_t *xv6_uart_base = NULL;
static uint8_t rx_queue[UART_QUEUE_LEN] = {};
static int rx_head = 0, rx_tail = 0;
static bool rx_irq_pending = false;

void xv6_plic_raise_irq(int irq);

static inline bool rx_queue_empty(void) {
  return rx_head == rx_tail;
}

static inline bool rx_queue_full(void) {
  return ((rx_tail + 1) % UART_QUEUE_LEN) == rx_head;
}

static void xv6_uart_putc(uint8_t ch) {
  putc(ch, stderr);
}

static void update_lsr(void) {
  xv6_uart_base[UART_LSR] = UART_LSR_TX_EMPTY | UART_LSR_TX_IDLE;
  if (!rx_queue_empty()) xv6_uart_base[UART_LSR] |= UART_LSR_RX_READY;
}

static void update_iir(void) {
  if ((xv6_uart_base[UART_IER] & UART_IER_RX_ENABLE) && !rx_queue_empty()) {
    xv6_uart_base[UART_ISR] = UART_IIR_RX_AVAILABLE;
  } else {
    xv6_uart_base[UART_ISR] = UART_IIR_NO_INT;
  }
}

static void sync_rx_state(void) {
  update_lsr();
  update_iir();
  if (!rx_irq_pending &&
      (xv6_uart_base[UART_IER] & UART_IER_RX_ENABLE) &&
      !rx_queue_empty()) {
    rx_irq_pending = true;
    xv6_plic_raise_irq(CONFIG_XV6_UART_IRQ);
  }
}

static void rx_enqueue(uint8_t ch) {
  if (rx_queue_full()) {
    Log("xv6-uart rx queue overflow, dropping byte 0x%02x", ch);
    return;
  }
  rx_queue[rx_tail] = ch;
  rx_tail = (rx_tail + 1) % UART_QUEUE_LEN;
}

static int rx_dequeue(void) {
  if (rx_queue_empty()) return -1;
  uint8_t ch = rx_queue[rx_head];
  rx_head = (rx_head + 1) % UART_QUEUE_LEN;
  return ch;
}

void xv6_uart_input_char(uint8_t ch) {
  if (xv6_uart_base == NULL) return;
  rx_enqueue(ch);
  sync_rx_state();
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
        int ch = rx_dequeue();
        xv6_uart_base[UART_RHR] = (ch < 0 ? 0xff : (uint8_t)ch);
        if (rx_queue_empty()) rx_irq_pending = false;
        sync_rx_state();
      }
      break;
    case UART_IER:
      if (is_write) sync_rx_state();
      else update_iir();
      break;
    case UART_LCR:
      break;
    case UART_FCR:
      if (is_write) {
        if (xv6_uart_base[UART_FCR] & 0x02) rx_head = rx_tail = 0;
        rx_irq_pending = false;
        sync_rx_state();
      } else {
        update_iir();
      }
      break;
    case UART_LSR:
      if (!is_write) sync_rx_state();
      break;
    default:
      break;
  }
}

void init_xv6_uart() {
  xv6_uart_base = new_space(UART_REG_NR);
  memset(xv6_uart_base, 0, UART_REG_NR);
  rx_head = rx_tail = 0;
  rx_irq_pending = false;
  xv6_uart_base[UART_ISR] = UART_IIR_NO_INT;
  sync_rx_state();
  add_mmio_map("xv6-uart", CONFIG_XV6_UART_MMIO, xv6_uart_base, UART_REG_NR, xv6_uart_io_handler);
}
