#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NANOS_DIR="$ROOT_DIR/nanos-lite"
NEMU_DIR="$ROOT_DIR/nemu"
RAMDISK_IMG="$ROOT_DIR/navy-apps/build/ramdisk.img"
NANOS_BIN="$NANOS_DIR/build/nanos-lite-riscv32-nemu.bin"
RUN_LOG="${RUN_LOG:-$ROOT_DIR/.verify_nemu_disk.log}"
NEMU_LOG="$NANOS_DIR/build/nemu-log.txt"
NEMU_CFG="$NEMU_DIR/include/config/auto.conf"
TIMEOUT_CMD="${TIMEOUT_CMD:-20s}"
IMAGE_SIZE_LIMIT="${IMAGE_SIZE_LIMIT:-10485760}"
SKIP_UPDATE="${SKIP_UPDATE:-0}"

ARCH="${ARCH:-riscv32-nemu}"
HAS_NAVY="${HAS_NAVY:-1}"
VME="${VME:-1}"

say() {
  printf '[verify-disk] %s\n' "$*"
}

fail() {
  printf '[verify-disk] FAIL: %s\n' "$*" >&2
  exit 1
}

cleanup_log() {
  perl -pe 's/\e\[[0-9;]*[A-Za-z]//g' "$1"
}

check_pattern() {
  local pattern="$1"
  local desc="$2"
  if grep -Fq "$pattern" "$RUN_LOG.clean"; then
    say "PASS: $desc"
  else
    fail "缺少关键信息: $desc ($pattern)"
  fi
}

check_any_pattern() {
  local pattern="$1"
  local desc="$2"
  shift 2
  local file
  for file in "$@"; do
    if [[ -f "$file" ]] && grep -Fq "$pattern" "$file"; then
      say "PASS: $desc"
      return 0
    fi
  done
  fail "缺少关键信息: $desc ($pattern)"
}

check_optional_pattern() {
  local pattern="$1"
  local desc="$2"
  shift 2
  local file
  for file in "$@"; do
    if [[ -f "$file" ]] && grep -Fq "$pattern" "$file"; then
      say "PASS: $desc"
      return 0
    fi
  done
  say "WARN: 未在日志中观察到 $desc"
}

say "切换 NEMU 到 host 配置"
make -C "$NEMU_DIR" ARCH=native riscv32-am_defconfig >/dev/null
check_any_pattern "CONFIG_HAS_DISK=y" "NEMU 配置已开启磁盘" "$NEMU_CFG"

if [[ "$SKIP_UPDATE" != "1" ]]; then
  say "更新 Navy 镜像"
  env CCACHE_DISABLE=1 make -C "$NANOS_DIR" ARCH="$ARCH" HAS_NAVY="$HAS_NAVY" VME="$VME" update -j1 >/dev/null
else
  say "跳过 update，直接使用现有镜像"
fi

[[ -f "$RAMDISK_IMG" ]] || fail "未找到磁盘镜像: $RAMDISK_IMG"
say "磁盘镜像存在: $RAMDISK_IMG"

say "生成 nanos-lite 镜像"
env CCACHE_DISABLE=1 make -C "$NANOS_DIR" ARCH="$ARCH" HAS_NAVY="$HAS_NAVY" VME="$VME" image -j2 >/dev/null

[[ -f "$NANOS_BIN" ]] || fail "未找到内核镜像: $NANOS_BIN"
BIN_SIZE="$(stat -c %s "$NANOS_BIN")"
say "内核镜像大小: ${BIN_SIZE} bytes"
if (( BIN_SIZE >= IMAGE_SIZE_LIMIT )); then
  fail "内核镜像过大，像是仍然内嵌了 ramdisk"
fi
say "PASS: 内核镜像未内嵌大 ramdisk"

say "启动 riscv32-nemu（dummy SDL, 超时 $TIMEOUT_CMD）"
set +e
timeout "$TIMEOUT_CMD" env SDL_VIDEODRIVER=dummy CCACHE_DISABLE=1 \
  make -C "$NANOS_DIR" ARCH="$ARCH" HAS_NAVY="$HAS_NAVY" VME="$VME" run -j2 \
  >"$RUN_LOG" 2>&1
RUN_RC=$?
set -e

cleanup_log "$RUN_LOG" > "$RUN_LOG.clean"

if [[ "$RUN_RC" != "0" && "$RUN_RC" != "124" ]]; then
  tail -n 80 "$RUN_LOG.clean" >&2
  fail "运行失败，退出码 $RUN_RC"
fi

check_pattern "disk info: blksz =" "nanos-lite 识别到磁盘"
check_pattern "Finish initialization" "nanos-lite 完成初始化"
check_optional_pattern "Add mmio map 'disk'" "NEMU 注册了磁盘 MMIO" "$RUN_LOG.clean" "$NEMU_LOG"
check_optional_pattern "disk image: $RAMDISK_IMG" "NEMU 打开了外部磁盘镜像" "$RUN_LOG.clean" "$NEMU_LOG"

say "验证通过"
say "日志文件: $RUN_LOG.clean"
