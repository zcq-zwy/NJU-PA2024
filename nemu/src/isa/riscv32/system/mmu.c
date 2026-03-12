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
#include <memory/vaddr.h>
#include <memory/paddr.h>

#define SATP_MODE_SV32 (1u << 31)
#define SATP_PPN_MASK  0x003fffffu
#define PTE_V          0x001
#define PTE_R          0x002
#define PTE_W          0x004
#define PTE_X          0x008
#define PTE_U          0x010
#define PTE_A          0x040
#define PTE_D          0x080

static inline paddr_t pte_addr(word_t pte) {
  return (pte >> 10) << 12;
}

paddr_t isa_mmu_translate(vaddr_t vaddr, int len, int type) {
  (void)len;
  if ((cpu.satp & SATP_MODE_SV32) == 0) {
    return vaddr;
  }

  paddr_t pgdir = (cpu.satp & SATP_PPN_MASK) << 12;
  int vpn1 = BITS(vaddr, 31, 22);
  int vpn0 = BITS(vaddr, 21, 12);
  int off  = BITS(vaddr, 11, 0);

  word_t pde = paddr_read(pgdir + vpn1 * sizeof(word_t), sizeof(word_t));
  if (!(pde & PTE_V)) return MEM_RET_FAIL;
  if (pde & (PTE_R | PTE_W | PTE_X)) return MEM_RET_FAIL;

  paddr_t pt = pte_addr(pde);
  paddr_t pte_addr_pa = pt + vpn0 * sizeof(word_t);
  word_t pte = paddr_read(pte_addr_pa, sizeof(word_t));
  if (!(pte & PTE_V)) return MEM_RET_FAIL;
  if (cpu.priv == RISCV_PRIV_U && !(pte & PTE_U)) return MEM_RET_FAIL;

  switch (type) {
    case MEM_TYPE_IFETCH:
      if (!(pte & PTE_X)) return MEM_RET_FAIL;
      break;
    case MEM_TYPE_READ:
      if (!(pte & PTE_R) && !((cpu.mstatus & MSTATUS_MXR) && (pte & PTE_X))) {
        return MEM_RET_FAIL;
      }
      break;
    case MEM_TYPE_WRITE:
      if (!(pte & PTE_W)) return MEM_RET_FAIL;
      break;
    default:
      break;
  }

  word_t new_pte = pte | PTE_A;
  if (type == MEM_TYPE_WRITE) new_pte |= PTE_D;
  if (new_pte != pte) {
    paddr_write(pte_addr_pa, sizeof(word_t), new_pte);
    pte = new_pte;
  }

  paddr_t pa = pte_addr(pte) | off;
  return pa;
}
