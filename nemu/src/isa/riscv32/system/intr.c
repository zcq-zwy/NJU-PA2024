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

#define IRQ_TIMER 0x80000007u

static inline word_t mstatus_with_mpp(word_t mstatus, word_t priv) {
  return (mstatus & ~MSTATUS_MPP_MASK) | ((priv & 0x3) << MSTATUS_MPP_SHIFT);
}

word_t isa_raise_intr(word_t NO, vaddr_t epc) {
  if (cpu.mstatus & MSTATUS_MIE) cpu.mstatus |= MSTATUS_MPIE;
  else cpu.mstatus &= ~MSTATUS_MPIE;
  cpu.mstatus &= ~MSTATUS_MIE;
  cpu.mstatus = mstatus_with_mpp(cpu.mstatus, cpu.priv);
  cpu.priv = RISCV_PRIV_M;
  cpu.mcause = NO;
  cpu.mepc = epc;
  etrace_log(NO, epc, cpu.mtvec);
  return cpu.mtvec;
}

word_t isa_query_intr() {
  if (cpu.INTR && (cpu.mstatus & MSTATUS_MIE)) {
    cpu.INTR = false;
    return IRQ_TIMER;
  }
  return INTR_EMPTY;
}
