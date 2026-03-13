#!/usr/bin/env bash
set -euo pipefail

ROOT=/root/ics2024
XV6_HOME="$ROOT/third_party/xv6-rv32"
NEMU_HOME="$ROOT/nemu"
NO_REBUILD="${NO_REBUILD:-0}"

if [ "$#" -lt 1 ]; then
  echo "用法: $0 'nettest grade' 或 $0 'nettests'" >&2
  exit 1
fi

MAKE_CC="riscv64-linux-gnu-gcc -march=rv32ima_zicsr -mabi=ilp32"
MAKE_TOOLPREFIX="riscv64-linux-gnu-"
MAKE_CFLAGS="-Wall -Werror -O -fno-omit-frame-pointer -ggdb -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -march=rv32ima_zicsr -mabi=ilp32 -DNET_TESTS_PORT=26099"
MAKE_LDFLAGS="-melf32lriscv -z max-page-size=4096"

echo "[net-cmd] 清理旧的 NEMU 进程"
pkill -f riscv32-nemu-interpreter 2>/dev/null || true

cd "$XV6_HOME"
if [ "$NO_REBUILD" = "1" ]; then
  echo "[net-cmd] 跳过重建，复用当前 kernel/fs.img"
else
  echo "[net-cmd] 强制重建 xv6"
  rm -f fs.img user/_nettest user/nettest.o user/_nettests user/nettests.o kernel/kernel kernel/kernel.bin
  env CCACHE_DISABLE=1 make kernel/kernel fs.img \
    CC="$MAKE_CC" \
    TOOLPREFIX="$MAKE_TOOLPREFIX" \
    CFLAGS="$MAKE_CFLAGS" \
    LDFLAGS="$MAKE_LDFLAGS"
  riscv64-linux-gnu-objcopy -S -O binary kernel/kernel kernel/kernel.bin
fi

echo "[net-cmd] 执行命令: $*"
export XV6_HOME
export NEMU_HOME
python3 - "$@" <<'PY'
import os
import select
import subprocess
import sys
import time

commands = list(sys.argv[1:])
done_marker = "__XV6_NET_CMD_DONE__"
commands.append(f"echo {done_marker}")

proc = subprocess.Popen(
    [
        os.path.join(os.environ["NEMU_HOME"], "build", "riscv32-nemu-interpreter"),
        "-b",
        os.path.join(os.environ["XV6_HOME"], "kernel", "kernel.bin"),
    ],
    cwd=os.environ["NEMU_HOME"],
    env={
        **os.environ,
        "SDL_VIDEODRIVER": "dummy",
        "CCACHE_DISABLE": "1",
        "NEMU_XV6_UART_INPUT": "stdin",
    },
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
)

output = bytearray()
next_cmd = 0
deadline = time.time() + 300

def send_command(text):
    proc.stdin.write((text + "\n").encode())
    proc.stdin.flush()

while True:
    if time.time() > deadline:
        proc.terminate()
        raise SystemExit("超时: xv6 net 命令执行未完成")

    if proc.poll() is not None:
        try:
            data = os.read(proc.stdout.fileno(), 4096)
        except OSError:
            data = b""
        if data:
            output.extend(data)
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
        break

    ready, _, _ = select.select([proc.stdout], [], [], 0.2)
    if not ready:
        continue

    try:
        data = os.read(proc.stdout.fileno(), 4096)
    except OSError:
        data = b""
    if not data:
        continue

    output.extend(data)
    text = data.decode("utf-8", "replace")
    sys.stdout.write(text)
    sys.stdout.flush()

    whole = output.decode("utf-8", "replace")
    while next_cmd < len(commands) and whole.endswith("$ "):
        send_command(commands[next_cmd])
        next_cmd += 1
        time.sleep(0.05)
        break

    if done_marker in whole:
        proc.terminate()
        break
PY
