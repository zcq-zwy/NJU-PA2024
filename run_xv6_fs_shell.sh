#!/usr/bin/env bash
set -euo pipefail

ROOT=/root/ics2024
XV6_HOME="$ROOT/third_party/xv6-rv32"
NEMU_HOME="$ROOT/nemu"
NEMU_LOG="$ROOT/xv6-fs-nemu.log"

MAKE_CC="riscv64-linux-gnu-gcc -march=rv32ima_zicsr -mabi=ilp32"
MAKE_TOOLPREFIX="riscv64-linux-gnu-"
MAKE_CFLAGS="-Wall -Werror -O -fno-omit-frame-pointer -ggdb -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -march=rv32ima_zicsr -mabi=ilp32"
MAKE_LDFLAGS="-melf32lriscv -z max-page-size=4096"

echo "[fs-shell] 清理旧的 NEMU 进程"
pkill -f riscv32-nemu-interpreter 2>/dev/null || true

echo "[fs-shell] 强制重建 xv6 内核与 fs.img"
cd "$XV6_HOME"
rm -f fs.img user/_bigfile user/bigfile.o kernel/kernel kernel/kernel.bin

env CCACHE_DISABLE=1 make kernel/kernel fs.img \
  CC="$MAKE_CC" \
  TOOLPREFIX="$MAKE_TOOLPREFIX" \
  CFLAGS="$MAKE_CFLAGS" \
  LDFLAGS="$MAKE_LDFLAGS"

echo "[fs-shell] 生成 kernel.bin"
riscv64-linux-gnu-objcopy -S -O binary kernel/kernel kernel/kernel.bin

echo "[fs-shell] 启动 xv6 shell"
echo "[fs-shell] 进入后可测试: bigfile 1 / bigfile 10 / bigfile 299 / symlinktest"
echo "[fs-shell] 若退出后终端异常，可执行: stty sane"
echo "[fs-shell] NEMU 日志: $NEMU_LOG"
cd "$NEMU_HOME"

stty sane || true

if [ -r /dev/tty ]; then
  exec env XV6_HOME="$XV6_HOME" SDL_VIDEODRIVER=dummy NEMU_XV6_UART_RAW=1 \
    ./build/riscv32-nemu-interpreter -l "$NEMU_LOG" -b "$XV6_HOME/kernel/kernel.bin" < /dev/tty
fi

exec env XV6_HOME="$XV6_HOME" SDL_VIDEODRIVER=dummy NEMU_XV6_UART_RAW=1 \
  ./build/riscv32-nemu-interpreter -l "$NEMU_LOG" -b "$XV6_HOME/kernel/kernel.bin"
