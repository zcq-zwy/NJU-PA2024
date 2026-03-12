#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
XV6_HOME="${XV6_HOME:-$ROOT_DIR/third_party/xv6-rv32}"
NEMU_HOME="${NEMU_HOME:-$ROOT_DIR/nemu}"
TIMEOUT_SECS="${TIMEOUT_SECS:-20}"
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
  sleep 7
  printf 'echo __XV6_SMOKE_OK__\n'
  sleep 1
  printf 'ls\n'
  sleep 1
  printf 'cat README\n'
  sleep 1
  printf 'wc README\n'
  sleep 1
  printf 'grep xv6 README\n'
  sleep 1
  printf 'mkdir smoke_dir\n'
  sleep 1
  printf 'ls\n'
  sleep 1
  printf 'rm smoke_dir\n'
  sleep 1
  printf 'ls\n'
  sleep 1
} >"$fifo" &
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
check_log "__XV6_SMOKE_OK__" "echo command output"
check_log "README" "README appears in directory listing"
check_log "xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix" "cat README output"
check_log "1982 README" "wc README output"
check_log "xv6" "grep README output"
check_log "smoke_dir" "mkdir result visible in ls"

if grep -Eq '^smoke_dir[[:space:]]' "$log"; then
  last_smoke_line="$(grep -En '^smoke_dir[[:space:]]' "$log" | tail -n1 | cut -d: -f1)"
  total_lines="$(wc -l < "$log")"
  if tail -n "$(( total_lines - last_smoke_line ))" "$log" | grep -Eq '^smoke_dir[[:space:]]'; then
    echo "[smoke] smoke_dir still visible after rm" >&2
    cat "$log" >&2
    exit 1
  fi
fi

echo "[smoke] passed"
if [[ "$KEEP_TMP" == "1" ]]; then
  echo "[smoke] log: $log"
fi
