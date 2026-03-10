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
#include <cpu/difftest.h>
#include "../local-include/reg.h"

bool isa_difftest_checkregs(CPU_state *ref_r, vaddr_t pc) {
  for (int i = 0; i < ARRLEN(cpu.gpr); i++) {
    if (cpu.gpr[i] != ref_r->gpr[i]) {
      Log("difftest mismatch at pc=" FMT_WORD ": %s, dut=" FMT_WORD ", ref=" FMT_WORD,
          pc, reg_name(i), cpu.gpr[i], ref_r->gpr[i]);
      return false;
    }
  }

  if (cpu.pc != ref_r->pc) {
    Log("difftest mismatch at pc=" FMT_WORD ": pc, dut=" FMT_WORD ", ref=" FMT_WORD,
        pc, cpu.pc, ref_r->pc);
    return false;
  }

  if (!difftest_check_reg("mstatus", pc, ref_r->mstatus, cpu.mstatus)) return false;
  if (!difftest_check_reg("mtvec",   pc, ref_r->mtvec,   cpu.mtvec  )) return false;
  if (!difftest_check_reg("mepc",    pc, ref_r->mepc,    cpu.mepc   )) return false;
  if (!difftest_check_reg("mcause",  pc, ref_r->mcause,  cpu.mcause )) return false;
  if (!difftest_check_reg("satp",    pc, ref_r->satp,    cpu.satp   )) return false;

  return true;
}

void isa_difftest_attach() {
}
