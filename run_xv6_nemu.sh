#!/usr/bin/env bash
set -euo pipefail

XV6_HOME="${XV6_HOME:-/root/ics2024/third_party/xv6-rv32}"
NEMU_HOME="${NEMU_HOME:-/root/ics2024/nemu}"

env CCACHE_DISABLE="${CCACHE_DISABLE:-1}" make -C "$XV6_HOME" kernel/kernel fs.img \
  CC='riscv64-linux-gnu-gcc -march=rv32ima_zicsr -mabi=ilp32' \
  TOOLPREFIX='riscv64-linux-gnu-' \
  CFLAGS='-Wall -Werror -O -fno-omit-frame-pointer -ggdb -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -march=rv32ima_zicsr -mabi=ilp32' \
  LDFLAGS='-melf32lriscv -z max-page-size=4096'

riscv64-linux-gnu-objcopy -S -O binary \
  "$XV6_HOME/kernel/kernel" \
  "$XV6_HOME/kernel/kernel.bin"

make -C "$NEMU_HOME" ARCH=native riscv32-xv6_defconfig
env CCACHE_DISABLE="${CCACHE_DISABLE:-1}" make -C "$NEMU_HOME" ISA=riscv32 -j2

cd "$NEMU_HOME"
if [ -r /dev/tty ]; then
  env XV6_HOME="$XV6_HOME" SDL_VIDEODRIVER=dummy \
    ./build/riscv32-nemu-interpreter -b "$XV6_HOME/kernel/kernel.bin" < /dev/tty
else
  env XV6_HOME="$XV6_HOME" SDL_VIDEODRIVER=dummy \
    ./build/riscv32-nemu-interpreter -b "$XV6_HOME/kernel/kernel.bin"
fi
