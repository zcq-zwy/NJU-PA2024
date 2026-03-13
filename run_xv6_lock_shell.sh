#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
XV6_HOME="${XV6_HOME:-$ROOT_DIR/third_party/xv6-rv32}"
NEMU_HOME="${NEMU_HOME:-$ROOT_DIR/nemu}"
SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
CCACHE_DISABLE="${CCACHE_DISABLE:-1}"
LOCKLAB_YEAR="${1:-2025}"

if [[ "$LOCKLAB_YEAR" != "2024" && "$LOCKLAB_YEAR" != "2025" ]]; then
  echo "usage: $0 [2024|2025]" >&2
  exit 1
fi

env CCACHE_DISABLE="$CCACHE_DISABLE" make -C "$XV6_HOME" clean

env CCACHE_DISABLE="$CCACHE_DISABLE" make -C "$XV6_HOME" kernel/kernel fs.img \
  LOCKLAB_YEAR="$LOCKLAB_YEAR" \
  CC='riscv64-linux-gnu-gcc -march=rv32ima_zicsr -mabi=ilp32' \
  TOOLPREFIX='riscv64-linux-gnu-' \
  CFLAGS="-Wall -Werror -O -fno-omit-frame-pointer -ggdb -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -march=rv32ima_zicsr -mabi=ilp32 -DNET_TESTS_PORT=26099 -DLOCKLAB_YEAR=$LOCKLAB_YEAR" \
  LDFLAGS='-melf32lriscv -z max-page-size=4096'

riscv64-linux-gnu-objcopy -S -O binary \
  "$XV6_HOME/kernel/kernel" \
  "$XV6_HOME/kernel/kernel.bin"

make -C "$NEMU_HOME" ARCH=native riscv32-xv6_defconfig
env CCACHE_DISABLE="$CCACHE_DISABLE" make -C "$NEMU_HOME" ISA=riscv32 -j2

echo "[lock-shell] 启动交互式 xv6 shell"
echo "[lock-shell] 年份: $LOCKLAB_YEAR"
if [[ "$LOCKLAB_YEAR" == "2024" ]]; then
  echo "[lock-shell] 你可以手动输入: stats / kalloctest / bcachetest / usertests sbrkmuch / usertests -q"
else
  echo "[lock-shell] 你可以手动输入: stats / kalloctest / rwlktest / usertests sbrkmuch / usertests -q"
fi
echo "[lock-shell] 若退出后终端异常，可执行: stty sane"

cd "$NEMU_HOME"
stty sane || true
unset NEMU_XV6_UART_INPUT NEMU_XV6_UART_RAW
exec env XV6_HOME="$XV6_HOME" SDL_VIDEODRIVER="$SDL_VIDEODRIVER" \
  ./build/riscv32-nemu-interpreter -b "$XV6_HOME/kernel/kernel.bin"
