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

#include <isa.h>
#include <monitor/etrace.h>

#define IRQ_S_SOFT  0x80000001u
#define IRQ_S_EXT   0x80000009u
#define IRQ_TIMER 0x80000007u

static inline word_t mstatus_with_mpp(word_t mstatus, word_t priv) {
  return (mstatus & ~MSTATUS_MPP_MASK) | ((priv & 0x3) << MSTATUS_MPP_SHIFT);
}

static inline word_t mstatus_with_spp(word_t mstatus, word_t priv) {
  return (priv == RISCV_PRIV_S) ? (mstatus | SSTATUS_SPP) : (mstatus & ~SSTATUS_SPP);
}

static inline bool is_interrupt(word_t cause) {
  return (cause >> 31) != 0;
}

static word_t trap_tval = 0;

void isa_set_trap_tval(word_t tval) {
  trap_tval = tval;
}

word_t isa_raise_intr(word_t NO, vaddr_t epc) {
  word_t deleg_mask = is_interrupt(NO) ? cpu.mideleg : cpu.medeleg;
  bool delegated = ((deleg_mask >> (NO & 0x1f)) & 1) != 0 && cpu.priv != RISCV_PRIV_M;

  if (delegated) {
    if (cpu.mstatus & SSTATUS_SIE) cpu.mstatus |= SSTATUS_SPIE;
    else cpu.mstatus &= ~SSTATUS_SPIE;
    cpu.mstatus &= ~SSTATUS_SIE;
    cpu.mstatus = mstatus_with_spp(cpu.mstatus, cpu.priv);
    cpu.priv = RISCV_PRIV_S;
    cpu.scause = NO;
    cpu.sepc = epc;
    cpu.stval = trap_tval;
    etrace_log(NO, epc, cpu.stvec);
    trap_tval = 0;
    return cpu.stvec;
  }

  if (cpu.mstatus & MSTATUS_MIE) cpu.mstatus |= MSTATUS_MPIE;
  else cpu.mstatus &= ~MSTATUS_MPIE;
  cpu.mstatus &= ~MSTATUS_MIE;
  cpu.mstatus = mstatus_with_mpp(cpu.mstatus, cpu.priv);
  cpu.priv = RISCV_PRIV_M;
  cpu.mcause = NO;
  cpu.mepc = epc;
  cpu.mtval = trap_tval;
  etrace_log(NO, epc, cpu.mtvec);
  trap_tval = 0;
  return cpu.mtvec;
}

word_t isa_query_intr() {
  if ((cpu.mip & cpu.mie & (1u << 7)) && (cpu.mstatus & MSTATUS_MIE)) {
    return IRQ_TIMER;
  }
  if (cpu.INTR && (cpu.mstatus & MSTATUS_MIE)) {
    cpu.INTR = false;
    return IRQ_TIMER;
  }
  if ((cpu.mip & cpu.mie & (1u << 1)) && (cpu.mstatus & SSTATUS_SIE)) {
    return IRQ_S_SOFT;
  }
  if ((cpu.mip & cpu.mie & (1u << 9)) && (cpu.mstatus & SSTATUS_SIE)) {
    return IRQ_S_EXT;
  }
  return INTR_EMPTY;
}
