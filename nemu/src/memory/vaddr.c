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

static inline bool is_cross_page(vaddr_t addr, int len) {
  return ((addr & PAGE_MASK) + len) > PAGE_SIZE;
}

static inline paddr_t vaddr_translate(vaddr_t addr, int len, int type) {
  int mode = isa_mmu_check(addr, len, type);
  if (mode == MMU_DIRECT) return addr;
  if (mode == MMU_TRANSLATE) return isa_mmu_translate(addr, len, type);
  return MEM_RET_FAIL;
}

word_t vaddr_ifetch(vaddr_t addr, int len) {
  if (is_cross_page(addr, len)) {
    int len1 = PAGE_SIZE - (addr & PAGE_MASK);
    int len2 = len - len1;
    word_t lo = vaddr_ifetch(addr, len1);
    word_t hi = vaddr_ifetch(addr + len1, len2);
    return lo | (hi << (len1 * 8));
  }

  paddr_t paddr = vaddr_translate(addr, len, MEM_TYPE_IFETCH);
  Assert(paddr != MEM_RET_FAIL, "ifetch translate fail: va=%x len=%d satp=%x", addr, len, cpu.satp);
  return paddr_read(paddr, len);
}

word_t vaddr_read(vaddr_t addr, int len) {
  if (is_cross_page(addr, len)) {
    int len1 = PAGE_SIZE - (addr & PAGE_MASK);
    int len2 = len - len1;
    word_t lo = vaddr_read(addr, len1);
    word_t hi = vaddr_read(addr + len1, len2);
    return lo | (hi << (len1 * 8));
  }

  paddr_t paddr = vaddr_translate(addr, len, MEM_TYPE_READ);
  Assert(paddr != MEM_RET_FAIL, "read translate fail: va=%x len=%d satp=%x", addr, len, cpu.satp);
  return paddr_read(paddr, len);
}

void vaddr_write(vaddr_t addr, int len, word_t data) {
  if (is_cross_page(addr, len)) {
    int len1 = PAGE_SIZE - (addr & PAGE_MASK);
    int len2 = len - len1;
    vaddr_write(addr, len1, data);
    vaddr_write(addr + len1, len2, data >> (len1 * 8));
    return;
  }

  paddr_t paddr = vaddr_translate(addr, len, MEM_TYPE_WRITE);
  Assert(paddr != MEM_RET_FAIL, "write translate fail: va=%x len=%d data=%x satp=%x", addr, len, data, cpu.satp);
  paddr_write(paddr, len, data);
}
