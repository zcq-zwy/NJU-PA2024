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
#include "local-include/reg.h"

const char *regs[] = {
  "$0", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
  "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
  "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
  "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};


void isa_reg_display() {
    // RVE 只有 16 个通用寄存器；普通 RV32I/RV64I 是 32 个
    int n = MUXDEF(CONFIG_RVE, 16, 32);

    // 逐个打印通用寄存器：名字 + 数值
    for (int i = 0; i < n; i++) {
      printf("%-4s\t" FMT_WORD "\n", reg_name(i), gpr(i));
    }

    // 最后单独打印 PC
    printf("pc  \t" FMT_WORD "\n", cpu.pc);
  }

word_t isa_reg_str2val(const char *s, bool *success) {
  return 0;
}
