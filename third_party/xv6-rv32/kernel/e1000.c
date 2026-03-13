//
// xv6 侧 e1000 驱动。
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

static volatile uint32 *regs;
static struct spinlock e1000_lock;

void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");
  regs = xregs;

  regs[E1000_IMS] = 0;
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0;
  __sync_synchronize();

  memset(tx_ring, 0, sizeof(tx_ring));
  for(i = 0; i < TX_RING_SIZE; i++){
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint32)tx_ring;
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = 0;
  regs[E1000_TDT] = 0;

  memset(rx_ring, 0, sizeof(rx_ring));
  for(i = 0; i < RX_RING_SIZE; i++){
    rx_mbufs[i] = mbufalloc(0);
    if(rx_mbufs[i] == 0)
      panic("e1000");
    rx_ring[i].addr = (uint32)rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint32)rx_ring;
  regs[E1000_RDLEN] = sizeof(rx_ring);
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;

  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA + 1] = 0x5634 | (1 << 31);
  for(i = 0; i < 4096 / 32; i++)
    regs[E1000_MTA + i] = 0;

  regs[E1000_TCTL] = E1000_TCTL_EN |
                     E1000_TCTL_PSP |
                     (0x10 << E1000_TCTL_CT_SHIFT) |
                     (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8 << 10) | (6 << 20);

  regs[E1000_RCTL] = E1000_RCTL_EN |
                     E1000_RCTL_BAM |
                     E1000_RCTL_SZ_2048 |
                     E1000_RCTL_SECRC;
  regs[E1000_RDTR] = 0;
  regs[E1000_RADV] = 0;
  regs[E1000_IMS] = (1 << 7);
}

int
e1000_transmit(struct mbuf *m)
{
  uint32 index;

  acquire(&e1000_lock);
  index = regs[E1000_TDT];
  if((tx_ring[index].status & E1000_TXD_STAT_DD) == 0){
    release(&e1000_lock);
    return -1;
  }
  if(tx_mbufs[index] != 0)
    mbuffree(tx_mbufs[index]);
  tx_mbufs[index] = m;
  tx_ring[index].addr = (uint32)m->head;
  tx_ring[index].length = m->len;
  tx_ring[index].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  tx_ring[index].status = 0;
  regs[E1000_TDT] = (index + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  uint32 index;

  for(;;){
    index = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    if((rx_ring[index].status & E1000_RXD_STAT_DD) == 0)
      break;
    rx_mbufs[index]->len = rx_ring[index].length;
    net_rx(rx_mbufs[index]);
    rx_mbufs[index] = mbufalloc(0);
    if(rx_mbufs[index] == 0)
      panic("e1000");
    rx_ring[index].addr = (uint32)rx_mbufs[index]->head;
    rx_ring[index].status = 0;
    regs[E1000_RDT] = index;
  }
}

void
e1000_intr(void)
{
  regs[E1000_ICR] = 0xffffffff;
  e1000_recv();
}
