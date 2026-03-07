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

#ifndef __ISA_X86_H__
#define __ISA_X86_H__

#include <common.h>


/* TODO: Re-organize the `CPU_state' structure to match the register
 * encoding scheme in i386 instruction format. For example, if we
 * access cpu.gpr[3]._16, we will get the `bx' register; if we access
 * cpu.gpr[1]._8[1], we will get the 'ch' register. Hint: Use `union'.
 * For more details about the register encoding scheme, see i386 manual.
 */

 /**
 1. 最外层 x86_CPU_state
     表示 CPU 的可见状态：通用寄存器 + pc。
  2. 中间匿名 union（关键）
     让两种访问方式共用同一块内存：

  - 数组视角：gpr[0..7]
  - 命名字段视角：eax/ecx/.../edi

  所以写 gpr[0]._32 后，eax 会同步变化（同址别名）。

  3. gpr[i] 里的内层 union
     同一个寄存器的不同位宽视图：

  - _32：32 位（如 EAX）
  - _16：低 16 位（如 AX）
  - _8[0]：低 8 位（AL）
  - _8[1]：次低 8 位（AH）

  这正是 x86 寄存器别名关系。
  1. 同址别名
     gpr[8] 和 eax/ecx/.../edi 必须放在同一个匿名 union 里，保证是同一块内存的两种访问方式。
  2. 寄存器顺序一致
     struct { eax, ecx, edx, ebx, esp, ebp, esi, edi; } 的顺序要和 R_EAX...R_EDI 枚举完全一致。
  3. 子寄存器映射正确

  - reg_w(i) 必须是对应 32 位寄存器低 16 位。
  - reg_b 要满足：
      - AL/AH -> EAX 的 bit[7:0]/bit[15:8]
      - BL/BH -> EBX
      - CL/CH -> ECX
      - DL/DH -> EDX

  一句话：顺序对、同址对、位切片对，reg_test() 就能过。

  */

typedef struct {
    union {
      union {
        uint32_t _32;
        uint16_t _16;
        uint8_t  _8[2];
      } gpr[8];

      struct {
        uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
      };
    };
    vaddr_t pc;
    uint16_t cs;
    uint32_t eflags;
  } x86_CPU_state;

// decode
typedef struct {
  uint8_t inst[16];
  uint8_t *p_inst;
} x86_ISADecodeInfo;

enum { R_EAX, R_ECX, R_EDX, R_EBX, R_ESP, R_EBP, R_ESI, R_EDI };
enum { R_AX, R_CX, R_DX, R_BX, R_SP, R_BP, R_SI, R_DI };
enum { R_AL, R_CL, R_DL, R_BL, R_AH, R_CH, R_DH, R_BH };

#define isa_mmu_check(vaddr, len, type) (MMU_DIRECT)
#endif
