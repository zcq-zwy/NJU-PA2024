#include "cpu.h"
#include "cpu-internal.h"
#include "memory.h"
#include "ppu.h"
#include "psg.h"
#include "mmc.h"

static inline byte mem_fast_readb(word address) {
  switch (address >> 13) {
    case 0: return cpu_ram_read(address & 0x07FF);
    case 1: return ppuio_read(address);
    case 2: return psgio_read(address);
    case 3: return cpu_ram_read(address & 0x1FFF);
    default: return mmc_read(address);
  }
}

static inline void mem_fast_writeb(word address, byte data) {
  if (address == 0x4014) {
    word base = ((word)data) << 8;
    for (int i = 0; i < 256; i++) {
      ppu_sprram_write(CPU_RAM[(base + i) & 0x07FF]);
    }
    return;
  }

  switch (address >> 13) {
    case 0: cpu_ram_write(address & 0x07FF, data); return;
    case 1: ppuio_write(address, data); return;
    case 2: psgio_write(address, data); return;
    case 3: cpu_ram_write(address & 0x1FFF, data); return;
    default: mmc_write(address, data); return;
  }
}

static inline word mem_fast_readw(word address) {
  return mem_fast_readb(address) + (mem_fast_readb(address + 1) << 8);
}

static inline void mem_fast_writew(word address, word data) {
  mem_fast_writeb(address, data & 0xFF);
  mem_fast_writeb(address + 1, data >> 8);
}

static inline byte cpu_fetchb(word address) {
  return (address & 0x8000) ? memory[address] : mem_fast_readb(address);
}

static inline word cpu_fetchw(word address) {
  return cpu_fetchb(address) + (cpu_fetchb(address + 1) << 8);
}

#define memory_readb  mem_fast_readb
#define memory_writeb mem_fast_writeb
#define memory_readw  mem_fast_readw
#define memory_writew mem_fast_writew

// CPU Addressing Modes

void cpu_address_implied() { }

void cpu_address_immediate() {
  op_value = cpu_fetchb(cpu.PC);
  cpu.PC++;
}

void cpu_address_zero_page() {
  op_address = cpu_fetchb(cpu.PC);
  op_value = CPU_RAM[op_address];
  cpu.PC++;
}

void cpu_address_zero_page_x() {
  op_address = (cpu_fetchb(cpu.PC) + cpu.X) & 0xFF;
  op_value = CPU_RAM[op_address];
  cpu.PC++;
}

void cpu_address_zero_page_y() {
  op_address = (cpu_fetchb(cpu.PC) + cpu.Y) & 0xFF;
  op_value = CPU_RAM[op_address];
  cpu.PC++;
}

void cpu_address_absolute() {
  op_address = cpu_fetchw(cpu.PC);
  op_value = memory_readb(op_address);
  cpu.PC += 2;
}

void cpu_address_absolute_x() {
  op_address = cpu_fetchw(cpu.PC) + cpu.X;
  op_value = memory_readb(op_address);
  cpu.PC += 2;

  if ((op_address >> 8) != (cpu.PC >> 8)) {
    op_cycles++;
  }
}

void cpu_address_absolute_y() {
  op_address = (cpu_fetchw(cpu.PC) + cpu.Y) & 0xFFFF;
  op_value = memory_readb(op_address);
  cpu.PC += 2;

  if ((op_address >> 8) != (cpu.PC >> 8)) {
    op_cycles++;
  }
}

void cpu_address_relative() {
  op_address = cpu_fetchb(cpu.PC);
  cpu.PC++;
  if (op_address & 0x80)
    op_address -= 0x100;
  op_address += cpu.PC;

  if ((op_address >> 8) != (cpu.PC >> 8)) {
    op_cycles++;
  }
}

void cpu_address_indirect() {
  word arg_addr = cpu_fetchw(cpu.PC);

  // The famous 6502 bug when instead of reading from $C0FF/$C100 it reads from $C0FF/$C000
  if ((arg_addr & 0xFF) == 0xFF) {
    // Buggy code
    op_address = (memory_readb(arg_addr & 0xFF00) << 8) + memory_readb(arg_addr);
  }
  else {
    // Normal code
    op_address = memory_readw(arg_addr);
  }
  cpu.PC += 2;
}

void cpu_address_indirect_x() {
  byte arg_addr = cpu_fetchb(cpu.PC);
  op_address = (memory_readb((arg_addr + cpu.X + 1) & 0xFF) << 8) | memory_readb((arg_addr + cpu.X) & 0xFF);
  op_value = memory_readb(op_address);
  cpu.PC++;
}

void cpu_address_indirect_y() {
  byte arg_addr = cpu_fetchb(cpu.PC);
  op_address = (((memory_readb((arg_addr + 1) & 0xFF) << 8) | memory_readb(arg_addr)) + cpu.Y) & 0xFFFF;
  op_value = memory_readb(op_address);
  cpu.PC++;

  if ((op_address >> 8) != (cpu.PC >> 8)) {
    op_cycles++;
  }
}
