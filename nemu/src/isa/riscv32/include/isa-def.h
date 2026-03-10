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

/**
 * @file isa-def.h
 * @brief ISA-specific definitions for RISC-V
  - #ifndef ... #define ... #endif：头文件保护，防止重复包含。
  - gpr[MUXDEF(CONFIG_RVE, 16, 32)]：
      - 开 CONFIG_RVE（E 扩展）时寄存器数是 16
      - 开 CONFIG_RV64 时类型名叫 riscv64_CPU_state
      - 否则叫 riscv32_CPU_state
  - ISADecodeInfo 那段同理：根据 CONFIG_RV64 选择 riscv64_ISADecodeInfo 或
    riscv32_ISADecodeInfo。
  - isa_mmu_check(...) (MMU_DIRECT)：当前把 MMU 检查固定为“直通”，即地址翻译阶段默认不做复杂页
    表转换（PA 前期常见简化）。

  对你现在的 riscv32 配置，可近似理解为：

  typedef struct {
    word_t gpr[32];   // 若不是 RVE
    vaddr_t pc;
  } riscv32_CPU_state;

  typedef struct {
    uint32_t inst;
  } riscv32_ISADecodeInfo;

  如果你再打开 CONFIG_RVE，gpr[32] 会变成 gpr[16]。
  gpr 是 General Purpose Registers，即“通用寄存器组”。

  在这段里：

  word_t gpr[...];

  表示 CPU_state 里用一个数组保存所有通用寄存器的值。
  对 riscv32 常见是 32 个（x0~x31），开 RVE 时是 16 个。
  之后取寄存器值本质就是访问 cpu.gpr[i]。
 */

#ifndef __ISA_RISCV_H__
#define __ISA_RISCV_H__

#include <common.h>

enum {
  RISCV_PRIV_U = 0,
  RISCV_PRIV_M = 3,
};

#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_MPP_MASK  (3u << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_U     (RISCV_PRIV_U << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_M     (RISCV_PRIV_M << MSTATUS_MPP_SHIFT)
#define MSTATUS_SUM       (1u << 18)
#define MSTATUS_MXR       (1u << 19)

typedef struct {
  word_t gpr[MUXDEF(CONFIG_RVE, 16, 32)];
  vaddr_t pc;
  word_t mstatus, mtvec, mepc, mcause, satp;
  uint8_t priv;
} MUXDEF(CONFIG_RV64, riscv64_CPU_state, riscv32_CPU_state);

// decode
typedef struct {
  uint32_t inst;
} MUXDEF(CONFIG_RV64, riscv64_ISADecodeInfo, riscv32_ISADecodeInfo);

#define isa_mmu_check(vaddr, len, type) ((cpu.satp & (1u << 31)) ? MMU_TRANSLATE : MMU_DIRECT)

#endif
