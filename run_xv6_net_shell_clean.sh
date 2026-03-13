#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
NEMU_HOME="$ROOT_DIR/nemu"
NEMU_BIN="$NEMU_HOME/build/riscv32-nemu-interpreter"

echo "[net-shell-clean] 杀掉旧的 NEMU 进程"
pkill -f riscv32-nemu-interpreter 2>/dev/null || true

echo "[net-shell-clean] 恢复当前终端"
stty sane || true

echo "[net-shell-clean] 清理可能残留的环境变量"
unset NEMU_XV6_UART_INPUT
unset NEMU_XV6_UART_RAW
unset NEMU_XV6_UART_TRACE
unset NEMU_XV6_UART_TRACE_FILE

echo "[net-shell-clean] 清理旧日志和旧 NEMU 可执行文件"
rm -f \
  "$ROOT_DIR/xv6-net-input-debug.log" \
  "$ROOT_DIR/xv6-net-uart-trace.txt" \
  "$NEMU_BIN"

echo "[net-shell-clean] 重新进入 net shell"
exec "$ROOT_DIR/run_xv6_net_shell.sh"
