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

#include "local-include/reg.h"
#include <cpu/cpu.h>
#include <cpu/ifetch.h>
#include <cpu/decode.h>
#include <monitor/ftrace.h>

#define R(i) gpr(i)
#define Mr vaddr_read
#define Mw vaddr_write

enum {
  TYPE_I, TYPE_U, TYPE_S, TYPE_B, TYPE_J, TYPE_R,
  TYPE_N,
};

#define src1R() do { *src1 = R(rs1); } while (0)
#define src2R() do { *src2 = R(rs2); } while (0)
#define immI() do { *imm = SEXT(BITS(i, 31, 20), 12); } while (0)
#define immU() do { *imm = SEXT(BITS(i, 31, 12), 20) << 12; } while (0)
#define immS() do { *imm = SEXT((BITS(i, 31, 25) << 5) | BITS(i, 11, 7), 12); } while (0)
#define immB() do { *imm = SEXT((BITS(i, 31, 31) << 12) | (BITS(i, 7, 7) << 11) | (BITS(i, 30, 25) << 5) | (BITS(i, 11, 8) << 1), 13); } while (0)
#define immJ() do { *imm = SEXT((BITS(i, 31, 31) << 20) | (BITS(i, 19, 12) << 12) | (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1), 21); } while (0)

static void decode_operand(Decode *s, int *rd, word_t *src1, word_t *src2, word_t *imm, int type) {
  uint32_t i = s->isa.inst;
  int rs1 = BITS(i, 19, 15);
  int rs2 = BITS(i, 24, 20);
  *rd = BITS(i, 11, 7);
  switch (type) {
    case TYPE_I: src1R(); immI(); break;
    case TYPE_U: immU(); break;
    case TYPE_S: src1R(); src2R(); immS(); break;
    case TYPE_B: src1R(); src2R(); immB(); break;
    case TYPE_J: immJ(); break;
    case TYPE_R: src1R(); src2R(); break;
    case TYPE_N: break;
    default: panic("unsupported type = %d", type);
  }
}

extern uint64_t g_nr_guest_inst;

static inline word_t mstatus_get_sie(word_t mstatus) {
  return (mstatus & SSTATUS_SIE) ? 1 : 0;
}

static inline word_t mstatus_set_sie(word_t mstatus, word_t enable) {
  return enable ? (mstatus | SSTATUS_SIE) : (mstatus & ~SSTATUS_SIE);
}

static inline word_t mstatus_get_spie(word_t mstatus) {
  return (mstatus & SSTATUS_SPIE) ? 1 : 0;
}

static inline word_t mstatus_set_spie(word_t mstatus, word_t enable) {
  return enable ? (mstatus | SSTATUS_SPIE) : (mstatus & ~SSTATUS_SPIE);
}

static inline word_t mstatus_get_spp(word_t mstatus) {
  return (mstatus & SSTATUS_SPP) ? RISCV_PRIV_S : RISCV_PRIV_U;
}

static inline word_t mstatus_set_spp(word_t mstatus, word_t priv) {
  return (priv == RISCV_PRIV_S) ? (mstatus | SSTATUS_SPP) : (mstatus & ~SSTATUS_SPP);
}

static word_t csr_read(uint32_t addr) {
  switch (addr) {
    case 0x300: return cpu.mstatus;
    case 0x302: return cpu.medeleg;
    case 0x303: return cpu.mideleg;
    case 0x304: return cpu.mie;
    case 0x305: return cpu.mtvec;
    case 0x306: return cpu.mcounteren;
    case 0x340: return cpu.mscratch;
    case 0x341: return cpu.mepc;
    case 0x342: return cpu.mcause;
    case 0x343: return cpu.mtval;
    case 0x344: return cpu.mip;

    case 0x100: return cpu.mstatus & SSTATUS_MASK;
    case 0x104: return cpu.mie & cpu.mideleg;
    case 0x105: return cpu.stvec;
    case 0x140: return cpu.sscratch;
    case 0x141: return cpu.sepc;
    case 0x142: return cpu.scause;
    case 0x143: return cpu.stval;
    case 0x144: return cpu.mip & cpu.mideleg;

    case 0x180: return cpu.satp;
    case 0xc01: return (word_t)g_nr_guest_inst;
    case 0xf14: return 0;
    default: panic("unsupported CSR = 0x%x", addr);
  }
}

static void csr_write(uint32_t addr, word_t val) {
  switch (addr) {
    case 0x300: cpu.mstatus = val; return;
    case 0x302: cpu.medeleg = val; return;
    case 0x303: cpu.mideleg = val; return;
    case 0x304: cpu.mie = val; return;
    case 0x305: cpu.mtvec = val; return;
    case 0x306: cpu.mcounteren = val; return;
    case 0x340: cpu.mscratch = val; return;
    case 0x341: cpu.mepc = val; return;
    case 0x342: cpu.mcause = val; return;
    case 0x343: cpu.mtval = val; return;
    case 0x344: cpu.mip = val; return;

    case 0x100:
      cpu.mstatus = (cpu.mstatus & ~SSTATUS_MASK) | (val & SSTATUS_MASK);
      return;
    case 0x104:
      cpu.mie = (cpu.mie & ~cpu.mideleg) | (val & cpu.mideleg);
      return;
    case 0x105: cpu.stvec = val; return;
    case 0x140: cpu.sscratch = val; return;
    case 0x141: cpu.sepc = val; return;
    case 0x142: cpu.scause = val; return;
    case 0x143: cpu.stval = val; return;
    case 0x144:
      cpu.mip = (cpu.mip & ~cpu.mideleg) | (val & cpu.mideleg);
      return;

    case 0x180: cpu.satp = val; return;
    case 0xc01:
    case 0xf14:
      return;
    default: panic("unsupported CSR = 0x%x", addr);
  }
}

static inline word_t mstatus_get_mpp(word_t mstatus) {
  return (mstatus & MSTATUS_MPP_MASK) >> MSTATUS_MPP_SHIFT;
}

static inline word_t mstatus_set_mpp(word_t mstatus, word_t priv) {
  return (mstatus & ~MSTATUS_MPP_MASK) | ((priv & 0x3) << MSTATUS_MPP_SHIFT);
}

static int decode_exec(Decode *s) {
  s->dnpc = s->snpc;

#define INSTPAT_INST(s) ((s)->isa.inst)
#define INSTPAT_MATCH(s, name, type, ... /* execute body */ ) { \
  int rd = 0; \
  word_t src1 = 0, src2 = 0, imm = 0; \
  decode_operand(s, &rd, &src1, &src2, &imm, concat(TYPE_, type)); \
  __VA_ARGS__ ; \
}

  INSTPAT_START();

  INSTPAT("??????? ????? ????? ??? ????? 01101 11", lui   , U, R(rd) = imm);
  INSTPAT("??????? ????? ????? ??? ????? 00101 11", auipc , U, R(rd) = s->pc + imm);
  INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal   , J, R(rd) = s->snpc; s->dnpc = s->pc + imm; IFDEF(CONFIG_FTRACE, if (rd == 1 || rd == 5) ftrace_call(s->pc, s->dnpc)));
  INSTPAT("??????? ????? ????? 000 ????? 11001 11", jalr  , I, word_t t = src1 + imm; R(rd) = s->snpc; s->dnpc = t & ~1; IFDEF(CONFIG_FTRACE, if (rd == 0 && BITS(s->isa.inst, 19, 15) == 1 && imm == 0) ftrace_ret(s->pc, s->dnpc); else if (rd == 1 || rd == 5) ftrace_call(s->pc, s->dnpc)));

  INSTPAT("??????? ????? ????? 000 ????? 11000 11", beq   , B, if (src1 == src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 001 ????? 11000 11", bne   , B, if (src1 != src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 100 ????? 11000 11", blt   , B, if ((sword_t)src1 <  (sword_t)src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 101 ????? 11000 11", bge   , B, if ((sword_t)src1 >= (sword_t)src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 110 ????? 11000 11", bltu  , B, if (src1 <  src2) s->dnpc = s->pc + imm);
  INSTPAT("??????? ????? ????? 111 ????? 11000 11", bgeu  , B, if (src1 >= src2) s->dnpc = s->pc + imm);

  INSTPAT("??????? ????? ????? 000 ????? 00000 11", lb    , I, R(rd) = SEXT(Mr(src1 + imm, 1), 8));
  INSTPAT("??????? ????? ????? 001 ????? 00000 11", lh    , I, R(rd) = SEXT(Mr(src1 + imm, 2), 16));
  INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw    , I, R(rd) = Mr(src1 + imm, 4));
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lbu   , I, R(rd) = Mr(src1 + imm, 1));
  INSTPAT("??????? ????? ????? 101 ????? 00000 11", lhu   , I, R(rd) = Mr(src1 + imm, 2));

  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb    , S, Mw(src1 + imm, 1, src2));
  INSTPAT("??????? ????? ????? 001 ????? 01000 11", sh    , S, Mw(src1 + imm, 2, src2));
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw    , S, Mw(src1 + imm, 4, src2));

  INSTPAT("??????? ????? ????? 000 ????? 00100 11", addi  , I, R(rd) = src1 + imm);
  INSTPAT("??????? ????? ????? 010 ????? 00100 11", slti  , I, R(rd) = ((sword_t)src1 <  (sword_t)imm) ? 1 : 0);
  INSTPAT("??????? ????? ????? 011 ????? 00100 11", sltiu , I, R(rd) = (src1 < imm) ? 1 : 0);
  INSTPAT("??????? ????? ????? 100 ????? 00100 11", xori  , I, R(rd) = src1 ^ imm);
  INSTPAT("??????? ????? ????? 110 ????? 00100 11", ori   , I, R(rd) = src1 | imm);
  INSTPAT("??????? ????? ????? 111 ????? 00100 11", andi  , I, R(rd) = src1 & imm);
  INSTPAT("0000000 ????? ????? 001 ????? 00100 11", slli  , I, R(rd) = src1 << BITS(imm, 4, 0));
  INSTPAT("0000000 ????? ????? 101 ????? 00100 11", srli  , I, R(rd) = src1 >> BITS(imm, 4, 0));
  INSTPAT("0100000 ????? ????? 101 ????? 00100 11", srai  , I, R(rd) = (sword_t)src1 >> BITS(imm, 4, 0));

  INSTPAT("0000000 ????? ????? 000 ????? 01100 11", add   , R, R(rd) = src1 + src2);
  INSTPAT("0100000 ????? ????? 000 ????? 01100 11", sub   , R, R(rd) = src1 - src2);
  INSTPAT("0000000 ????? ????? 001 ????? 01100 11", sll   , R, R(rd) = src1 << BITS(src2, 4, 0));
  INSTPAT("0000000 ????? ????? 010 ????? 01100 11", slt   , R, R(rd) = ((sword_t)src1 <  (sword_t)src2) ? 1 : 0);
  INSTPAT("0000000 ????? ????? 011 ????? 01100 11", sltu  , R, R(rd) = (src1 < src2) ? 1 : 0);
  INSTPAT("0000000 ????? ????? 100 ????? 01100 11", xor   , R, R(rd) = src1 ^ src2);
  INSTPAT("0000000 ????? ????? 101 ????? 01100 11", srl   , R, R(rd) = src1 >> BITS(src2, 4, 0));
  INSTPAT("0100000 ????? ????? 101 ????? 01100 11", sra   , R, R(rd) = (sword_t)src1 >> BITS(src2, 4, 0));
  INSTPAT("0000000 ????? ????? 110 ????? 01100 11", or    , R, R(rd) = src1 | src2);
  INSTPAT("0000000 ????? ????? 111 ????? 01100 11", and   , R, R(rd) = src1 & src2);

  INSTPAT("0000001 ????? ????? 000 ????? 01100 11", mul   , R, R(rd) = (word_t)((sword_t)src1 * (sword_t)src2));
  INSTPAT("0000001 ????? ????? 001 ????? 01100 11", mulh  , R, R(rd) = (word_t)(((int64_t)(sword_t)src1 * (int64_t)(sword_t)src2) >> 32));
  INSTPAT("0000001 ????? ????? 010 ????? 01100 11", mulhsu, R, R(rd) = (word_t)(((int64_t)(sword_t)src1 * (uint64_t)src2) >> 32));
  INSTPAT("0000001 ????? ????? 011 ????? 01100 11", mulhu , R, R(rd) = (word_t)(((uint64_t)src1 * (uint64_t)src2) >> 32));
  INSTPAT("0000001 ????? ????? 100 ????? 01100 11", div   , R, if ((sword_t)src2 == 0) R(rd) = (word_t)-1; else if ((sword_t)src1 == (sword_t)0x80000000 && (sword_t)src2 == -1) R(rd) = src1; else R(rd) = (sword_t)src1 / (sword_t)src2);
  INSTPAT("0000001 ????? ????? 101 ????? 01100 11", divu  , R, R(rd) = (src2 == 0) ? (word_t)-1 : src1 / src2);
  INSTPAT("0000001 ????? ????? 110 ????? 01100 11", rem   , R, if ((sword_t)src2 == 0) R(rd) = src1; else if ((sword_t)src1 == (sword_t)0x80000000 && (sword_t)src2 == -1) R(rd) = 0; else R(rd) = (sword_t)src1 % (sword_t)src2);
  INSTPAT("0000001 ????? ????? 111 ????? 01100 11", remu  , R, R(rd) = (src2 == 0) ? src1 : (src1 % src2));

  INSTPAT("0000000 00000 00000 000 00000 11100 11", ecall , N,
      s->dnpc = isa_raise_intr(cpu.priv == RISCV_PRIV_U ? 8 : 11, s->pc));
  INSTPAT("0000000 00001 00000 000 00000 11100 11", ebreak, N, NEMUTRAP(s->pc, R(10)));
  INSTPAT("0011000 00010 00000 000 00000 11100 11", mret  , N,
      cpu.priv = mstatus_get_mpp(cpu.mstatus);
      if (cpu.mstatus & MSTATUS_MPIE) cpu.mstatus |= MSTATUS_MIE;
      else cpu.mstatus &= ~MSTATUS_MIE;
      cpu.mstatus |= MSTATUS_MPIE;
      cpu.mstatus = mstatus_set_mpp(cpu.mstatus, RISCV_PRIV_U);
      s->dnpc = cpu.mepc);
  INSTPAT("0001000 00010 00000 000 00000 11100 11", sret  , N,
      cpu.priv = mstatus_get_spp(cpu.mstatus);
      cpu.mstatus = mstatus_set_sie(cpu.mstatus, mstatus_get_spie(cpu.mstatus));
      cpu.mstatus = mstatus_set_spie(cpu.mstatus, 1);
      cpu.mstatus = mstatus_set_spp(cpu.mstatus, RISCV_PRIV_U);
      s->dnpc = cpu.sepc);
  INSTPAT("00001?? ????? ????? 010 ????? 01011 11", amoswap_w, R,
      word_t t = Mr(src1, 4);
      Mw(src1, 4, src2);
      R(rd) = t);
  INSTPAT("??????? ????? ????? 001 ????? 11100 11", csrrw , I,
      uint32_t csr_addr = BITS(s->isa.inst, 31, 20);
      word_t t = csr_read(csr_addr);
      csr_write(csr_addr, src1);
      R(rd) = t);
  INSTPAT("??????? ????? ????? 010 ????? 11100 11", csrrs , I,
      uint32_t csr_addr = BITS(s->isa.inst, 31, 20);
      word_t t = csr_read(csr_addr);
      if (BITS(s->isa.inst, 19, 15) != 0) csr_write(csr_addr, t | src1);
      R(rd) = t);
  // fence: for single-core in-order NEMU, treat as no-op
  INSTPAT("??????? ????? 00000 000 00000 00011 11", fence  , I, );
  // fence.i: also no-op in this simplified model
  INSTPAT("0000000 00000 00000 001 00000 00011 11", fence_i, N, );
  // sfence.vma: no TLB in current PA model, treat as no-op
  INSTPAT("0001001 ????? ????? 000 00000 11100 11", sfence_vma, R, );
  // inv 是全通配兜底，放在前面会把后面的指令全吞掉
  INSTPAT("??????? ????? ????? ??? ????? ????? ??", inv   , N, INV(s->pc));
  INSTPAT_END();

  R(0) = 0;
  return 0;
}

static int decode_exec_compressed(Decode *s) {
  s->dnpc = s->snpc;
  uint32_t i = s->isa.inst & 0xffff;
  int rd = BITS(i, 11, 7);
  word_t imm = SEXT((BITS(i, 12, 12) << 5) | BITS(i, 6, 2), 6);

  if ((i & 0x3) == 0x1 && BITS(i, 15, 13) == 0x2) {
    R(rd) = imm;
  } else {
    INV(s->pc);
  }

  R(0) = 0;
  return 0;
}

int isa_exec_once(Decode *s) {
  uint16_t inst16 = inst_fetch(&s->snpc, 2);
  if ((inst16 & 0x3) != 0x3) {
    s->isa.inst = inst16;
    return decode_exec_compressed(s);
  }

  s->snpc = s->pc;
  s->isa.inst = inst_fetch(&s->snpc, 4);
  return decode_exec(s);
}
