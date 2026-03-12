#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
XV6_HOME="${XV6_HOME:-$ROOT_DIR/third_party/xv6-rv32}"
NEMU_HOME="${NEMU_HOME:-$ROOT_DIR/nemu}"
TIMEOUT_SECS="${TIMEOUT_SECS:-35}"
SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
CCACHE_DISABLE="${CCACHE_DISABLE:-1}"
KEEP_TMP="${KEEP_TMP:-0}"

tmpdir="$(mktemp -d)"
fifo="$tmpdir/xv6.stdin"
log="$tmpdir/xv6-smoke.log"

cleanup() {
  if [[ "$KEEP_TMP" != "1" ]]; then
    rm -rf "$tmpdir"
  fi
}
trap cleanup EXIT

mkfifo "$fifo"

echo "[smoke] build xv6 and NEMU"
{
  env CCACHE_DISABLE="$CCACHE_DISABLE" make -C "$XV6_HOME" kernel/kernel fs.img \
    CC='riscv64-linux-gnu-gcc -march=rv32ima_zicsr -mabi=ilp32' \
    TOOLPREFIX='riscv64-linux-gnu-' \
    CFLAGS='-Wall -Werror -O -fno-omit-frame-pointer -ggdb -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -march=rv32ima_zicsr -mabi=ilp32' \
    LDFLAGS='-melf32lriscv -z max-page-size=4096'
  riscv64-linux-gnu-objcopy -S -O binary \
    "$XV6_HOME/kernel/kernel" \
    "$XV6_HOME/kernel/kernel.bin"
  make -C "$NEMU_HOME" ARCH=native riscv32-xv6_defconfig
  env CCACHE_DISABLE="$CCACHE_DISABLE" make -C "$NEMU_HOME" ISA=riscv32 -j2
} >"$tmpdir/build.log" 2>&1 || {
  cat "$tmpdir/build.log" >&2
  exit 1
}

if [[ ! -x "$NEMU_HOME/build/riscv32-nemu-interpreter" ]]; then
  echo "[smoke] NEMU binary missing" >&2
  cat "$tmpdir/build.log" >&2
  exit 1
fi

if [[ ! -f "$XV6_HOME/kernel/kernel.bin" ]]; then
  echo "[smoke] xv6 kernel.bin missing" >&2
  cat "$tmpdir/build.log" >&2
  exit 1
fi

echo "[smoke] run xv6 smoke commands"
{
  # xv6 在 NEMU 上启动较慢，首条命令必须等 shell 起来后再送入。
  sleep 12
  sleep 1
  printf 'pingpong\n'
  sleep 2
  printf 'primes\n'
  sleep 2
  printf 'find . README\n'
  sleep 2
  printf 'echo README | xargs cat\n'
  sleep 1
} >"$fifo" 2>/dev/null &
writer_pid=$!

set +e
(
  cd "$NEMU_HOME"
  timeout "${TIMEOUT_SECS}s" env \
    XV6_HOME="$XV6_HOME" \
    SDL_VIDEODRIVER="$SDL_VIDEODRIVER" \
    ./build/riscv32-nemu-interpreter \
    -b "$XV6_HOME/kernel/kernel.bin"
) <"$fifo" >"$log" 2>&1
run_status=$?
set -e

wait "$writer_pid" || true

if [[ $run_status -ne 0 && $run_status -ne 124 ]]; then
  echo "[smoke] xv6 run failed with status $run_status" >&2
  cat "$log" >&2
  exit 1
fi

check_log() {
  local pattern="$1"
  local message="$2"
  if ! grep -Fq "$pattern" "$log"; then
    echo "[smoke] missing: $message" >&2
    cat "$log" >&2
    exit 1
  fi
}

check_log "init: starting sh" "shell startup"
check_log "received ping" "pingpong child output"
check_log "received pong" "pingpong parent output"
check_log "prime 2" "primes output"
check_log "prime 31" "primes tail output"
check_log "./README" "find output"
check_log "xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix" "cat README output"

echo "[smoke] passed"
if [[ "$KEEP_TMP" == "1" ]]; then
  echo "[smoke] log: $log"
fi
