#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
XV6_HOME="${XV6_HOME:-$ROOT_DIR/third_party/xv6-rv32}"
NEMU_HOME="${NEMU_HOME:-$ROOT_DIR/nemu}"
SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
CCACHE_DISABLE="${CCACHE_DISABLE:-1}"
DEBUG_LOG="${DEBUG_LOG:-$ROOT_DIR/xv6-net-input-debug.log}"
TRACE_FILE="${TRACE_FILE:-$ROOT_DIR/xv6-net-uart-trace.txt}"

env CCACHE_DISABLE="$CCACHE_DISABLE" make -C "$XV6_HOME" kernel/kernel fs.img \
  CC='riscv64-linux-gnu-gcc -march=rv32ima_zicsr -mabi=ilp32' \
  TOOLPREFIX='riscv64-linux-gnu-' \
  CFLAGS='-Wall -Werror -O -fno-omit-frame-pointer -ggdb -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -march=rv32ima_zicsr -mabi=ilp32' \
  LDFLAGS='-melf32lriscv -z max-page-size=4096'

riscv64-linux-gnu-objcopy -S -O binary \
  "$XV6_HOME/kernel/kernel" \
  "$XV6_HOME/kernel/kernel.bin"

make -C "$NEMU_HOME" ARCH=native riscv32-xv6_defconfig >/dev/null
env CCACHE_DISABLE="$CCACHE_DISABLE" make -C "$NEMU_HOME" ISA=riscv32 -j2 >/dev/null

echo "[net-shell-debug] 日志文件: $DEBUG_LOG"
echo "[net-shell-debug] UART 跟踪: $TRACE_FILE"
echo "[net-shell-debug] 复现后把 UART 跟踪贴出来"

cd "$NEMU_HOME"
stty sane || true
unset NEMU_XV6_UART_INPUT NEMU_XV6_UART_RAW
rm -f "$TRACE_FILE"
exec env XV6_HOME="$XV6_HOME" SDL_VIDEODRIVER="$SDL_VIDEODRIVER" \
  NEMU_XV6_UART_TRACE=1 NEMU_XV6_UART_TRACE_FILE="$TRACE_FILE" \
  ./build/riscv32-nemu-interpreter -l "$DEBUG_LOG" -b "$XV6_HOME/kernel/kernel.bin"
