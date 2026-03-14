#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
NAVY_HOME=$(cd -- "$SCRIPT_DIR/../.." && pwd)
export NAVY_HOME

python3 -u - <<'PY'
import fcntl
import os
import re
import subprocess
import sys
import time
from pathlib import Path

root = Path(os.environ["NAVY_HOME"]).resolve()
fsimg = root / "fsimg"
env = {
    "NAVY_HOME": str(root),
    "LD_PRELOAD": str(root / "libs/libos/build/native.so"),
    "NWM_APP": "1",
}
apps = [
    ("Terminal", [str(fsimg / "bin/nterm")]),
    ("NSlider", [str(fsimg / "bin/nslider")]),
    ("Typing Game", [str(fsimg / "bin/typing-game")]),
    ("FCEUX", [str(fsimg / "bin/fceux"), "/share/games/nes/Mario.nes"]),
    ("NPlayer", [str(fsimg / "bin/nplayer")]),
    ("Pal", [str(fsimg / "bin/pal")]),
    ("ONScripter", [str(fsimg / "bin/onscripter"), "-r", "/share/games/planetarian"]),
]


def set_nonblock(fd: int) -> None:
    flags = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)


def finish_process(process: subprocess.Popen[bytes]) -> tuple[bytes, bytes]:
    try:
        output, error = process.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        output, error = process.communicate(timeout=2)
    return output[-300:], error[-300:]


def probe_app(name: str, argv: list[str]) -> tuple[int, int]:
    nwm_to_app_read, nwm_to_app_write = os.pipe()
    app_to_nwm_read, app_to_nwm_write = os.pipe()
    framebuffer_fd = os.memfd_create(f"nwm-test-{name}", 0)
    set_nonblock(app_to_nwm_read)

    def setup_child() -> None:
        os.dup2(nwm_to_app_read, 3, inheritable=True)
        os.dup2(app_to_nwm_write, 4, inheritable=True)
        os.dup2(framebuffer_fd, 5, inheritable=True)
        os.set_inheritable(3, True)
        os.set_inheritable(4, True)
        os.set_inheritable(5, True)
        for fd in (nwm_to_app_read, nwm_to_app_write, app_to_nwm_read, app_to_nwm_write, framebuffer_fd):
            if fd > 5:
                try:
                    os.close(fd)
                except OSError:
                    pass
        for fd in range(6, 32):
            try:
                os.close(fd)
            except OSError:
                pass

    process = subprocess.Popen(
        argv,
        cwd=str(fsimg),
        env=env,
        preexec_fn=setup_child,
        close_fds=False,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    os.close(nwm_to_app_read)
    os.close(app_to_nwm_write)

    buffer = bytearray()
    width = None
    height = None
    deadline = time.time() + 10
    while time.time() < deadline:
        if process.poll() is not None:
            output, error = finish_process(process)
            raise RuntimeError(
                f"exited-before-handshake rc={process.returncode} stdout={output!r} stderr={error!r}"
            )
        try:
            chunk = os.read(app_to_nwm_read, 1024)
        except BlockingIOError:
            time.sleep(0.05)
            continue
        if not chunk:
            time.sleep(0.05)
            continue
        buffer.extend(chunk)
        match = re.search(rb"(\d+)\s+(\d+)", bytes(buffer))
        if match:
            width = int(match.group(1))
            height = int(match.group(2))
            break

    if width is None or height is None:
        process.terminate()
        output, error = finish_process(process)
        raise RuntimeError(f"no-handshake proto={bytes(buffer)!r} stdout={output!r} stderr={error!r}")

    os.ftruncate(framebuffer_fd, width * height * 4)
    os.write(nwm_to_app_write, b"mmap ok")
    time.sleep(2)
    if process.poll() is not None:
        output, error = finish_process(process)
        raise RuntimeError(f"early-exit rc={process.returncode} stdout={output!r} stderr={error!r}")

    process.terminate()
    finish_process(process)

    for fd in (nwm_to_app_write, app_to_nwm_read, framebuffer_fd):
        try:
            os.close(fd)
        except OSError:
            pass

    return width, height


failed = False
for name, argv in apps:
    try:
        width, height = probe_app(name, argv)
        print(f"PASS {name}: handshake={width}x{height}")
    except Exception as error:
        failed = True
        print(f"FAIL {name}: {error}")

raise SystemExit(1 if failed else 0)
PY
