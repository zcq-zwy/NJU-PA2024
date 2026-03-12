#include <device/map.h>
#include <isa.h>

#define PLIC_SIZE 0x400000
#define PLIC_PENDING_OFF      0x1000
#define PLIC_SENABLE_OFF      0x2080
#define PLIC_SPRIORITY_OFF    0x201000
#define PLIC_SCLAIM_OFF       0x201004

static uint8_t *xv6_plic_base = NULL;
static uint32_t pending_mask = 0;
static uint32_t senable_mask = 0;
static uint32_t spriority = 0;

static inline uint32_t *plic_reg(uint32_t offset) {
  return (uint32_t *)(xv6_plic_base + offset);
}

static void xv6_plic_sync_irq() {
  if ((pending_mask & senable_mask) != 0) cpu.mip |= (1u << 9);
  else cpu.mip &= ~(1u << 9);
}

static void xv6_plic_io_handler(uint32_t offset, int len, bool is_write) {
  assert(len == 4);
  assert(offset + len <= PLIC_SIZE);
  switch (offset) {
    case PLIC_PENDING_OFF:
      if (!is_write) *plic_reg(offset) = pending_mask;
      break;
    case PLIC_SENABLE_OFF:
      if (is_write) senable_mask = *plic_reg(offset);
      else *plic_reg(offset) = senable_mask;
      break;
    case PLIC_SPRIORITY_OFF:
      if (is_write) spriority = *plic_reg(offset);
      else *plic_reg(offset) = spriority;
      break;
    case PLIC_SCLAIM_OFF:
      if (is_write) {
        uint32_t irq = *plic_reg(offset);
        pending_mask &= ~(1u << irq);
      } else {
        uint32_t claim = pending_mask & senable_mask;
        uint32_t irq = 0;
        if (claim != 0) irq = __builtin_ctz(claim);
        *plic_reg(offset) = irq;
      }
      break;
    default:
      break;
  }
  xv6_plic_sync_irq();
}

void init_xv6_plic() {
  xv6_plic_base = new_space(PLIC_SIZE);
  memset(xv6_plic_base, 0, PLIC_SIZE);
  add_mmio_map("xv6-plic", CONFIG_XV6_PLIC_MMIO, xv6_plic_base, PLIC_SIZE, xv6_plic_io_handler);
}

void xv6_plic_raise_irq(int irq) {
  assert(irq > 0 && irq < 32);
  pending_mask |= (1u << irq);
  xv6_plic_sync_irq();
}
