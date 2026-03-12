from __future__ import print_function

import json
import os
import random
import re
import select
import string
import subprocess
import sys
import time
from optparse import OptionParser

__all__ = []

ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
XV6_HOME = os.environ.get("XV6_HOME", ROOT_DIR)
NEMU_HOME = os.environ.get("NEMU_HOME", os.path.abspath(os.path.join(ROOT_DIR, "..", "..", "nemu")))
OBJCOPY = os.environ.get("OBJCOPY", "riscv64-linux-gnu-objcopy")
CCACHE_DISABLE = os.environ.get("CCACHE_DISABLE", "1")
SDL_VIDEODRIVER = os.environ.get("SDL_VIDEODRIVER", "dummy")

BUILD_READY = False


##################################################################
# Test structure
#

__all__ += ["test", "run_tests", "get_current_test"]

TESTS = []
TOTAL = POSSIBLE = 0
CURRENT_TEST = None
GRADES = {}
OPTIONS = None


def test(points, title=None, parent=None):
    def register_test(fn, title=title):
        if not title:
            assert fn.__name__.startswith("test_")
            title = fn.__name__[5:].replace("_", " ")

        def run_test():
            global TOTAL, POSSIBLE, CURRENT_TEST

            if run_test.complete:
                return run_test.ok
            run_test.complete = True

            failed = None
            CURRENT_TEST = run_test
            start = time.time()
            sys.stdout.write("== Test %s == " % title)
            sys.stdout.flush()
            try:
                if parent and not parent():
                    raise AssertionError("Parent failed: %s" % parent.__name__)
                fn()
            except AssertionError as exc:
                failed = str(exc)

            POSSIBLE += points
            if failed:
                status = color("red", "FAIL")
            else:
                status = color("green", "OK")
                TOTAL += points
            GRADES[title] = 0 if failed else points

            sys.stdout.write("%s" % status)
            if time.time() - start > 0.1:
                sys.stdout.write(" (%.1fs)" % (time.time() - start))
            sys.stdout.write("\n")
            if failed:
                sys.stdout.write("    %s\n" % failed.replace("\n", "\n    "))
            sys.stdout.flush()

            CURRENT_TEST = None
            run_test.ok = not failed
            return run_test.ok

        run_test.__name__ = fn.__name__
        run_test.title = title
        run_test.complete = False
        run_test.ok = False
        TESTS.append(run_test)
        return run_test

    return register_test


def run_tests():
    global OPTIONS

    parser = OptionParser(usage="usage: %prog [-v] [filters...]")
    parser.add_option("-v", "--verbose", action="store_true", help="print commands")
    parser.add_option("--color", choices=["never", "always", "auto"], default="auto")
    parser.add_option("--results", help="results file path")
    OPTIONS, args = parser.parse_args()

    build_env()

    filters = list(map(str.lower, args))
    for case in TESTS:
        if not filters or any(item in case.title.lower() for item in filters):
            case()

    if OPTIONS.results:
        with open(OPTIONS.results, "w") as result_file:
            result_file.write(json.dumps(GRADES))

    if not filters:
        print("Score: %d/%d" % (TOTAL, POSSIBLE))

    if TOTAL < POSSIBLE:
        sys.exit(1)


def get_current_test():
    if CURRENT_TEST is None:
      raise RuntimeError("No test is running")
    return CURRENT_TEST


##################################################################
# Assertions
#

__all__ += ["assert_equal", "assert_lines_match"]


def assert_equal(got, expect, msg=""):
    if got == expect:
        return
    prefix = (msg + "\n") if msg else ""
    raise AssertionError("%sgot:\n  %s\nexpected:\n  %s" % (
        prefix,
        str(got).replace("\n", "\n  "),
        str(expect).replace("\n", "\n  "),
    ))


def assert_lines_match(text, *regexps, **kw):
    no = kw.get("no", [])
    lines = text.splitlines()
    remain = list(regexps)
    bad = []

    for line in lines:
        remain = [rex for rex in remain if not re.match(rex, line)]
        if any(re.match(rex, line) for rex in no):
            bad.append(line)

    if not remain and not bad:
        return

    message = []
    if bad:
        message.append("unexpected lines:")
        message.extend(["  " + line for line in bad])
    if remain:
        message.append("missing regexps:")
        message.extend(["  " + rex for rex in remain])
    message.append("recent output:")
    message.extend(["  " + line for line in lines[-20:]])
    raise AssertionError("\n".join(message))


##################################################################
# Utilities
#

__all__ += [
    "check_time",
    "color",
    "random_str",
    "save",
    "shell_script",
    "stop_breakpoint",
]

COLORS = {"default": "\033[0m", "red": "\033[31m", "green": "\033[32m"}


def color(name, text):
    if OPTIONS and (OPTIONS.color == "always" or (OPTIONS.color == "auto" and os.isatty(1))):
        return COLORS[name] + text + COLORS["default"]
    return text


def random_str(length=8):
    alphabet = string.ascii_letters + string.digits
    return "".join(random.choice(alphabet) for _ in range(length))


def check_time():
    time_path = os.path.join(ROOT_DIR, "time.txt")
    try:
        with open(time_path) as time_file:
            data = time_file.read().strip()
    except OSError:
        raise AssertionError("Cannot read time.txt")

    if not re.match(r"^\d+$", data):
        raise AssertionError("time.txt does not contain a single integer")


def save(path):
    def monitor(runner):
        runner.save_path = os.path.join(ROOT_DIR, path)
    return monitor


def shell_script(lines, wait_for=None):
    def monitor(runner):
        state = {
            "commands": list(lines),
            "next_index": 0,
        }

        def on_output():
            prompt_count = runner.qemu.output.count("$ ")
            at_prompt = runner.qemu.output.endswith("$ ")

            if wait_for:
                lines = runner.qemu.output.splitlines()
                if any(line == wait_for for line in lines):
                    runner.request_stop(0.2)
                    return

            # 只要 shell 当前停在提示符，就发送下一条命令。
            while at_prompt and state["next_index"] < len(state["commands"]) and prompt_count >= state["next_index"] + 1:
                runner.qemu.write(state["commands"][state["next_index"]] + "\n")
                state["next_index"] += 1
                return

            # 全部命令发完，并且 shell 已经重新回到提示符，就可以结束了。
            if wait_for is None and at_prompt and state["next_index"] >= len(state["commands"]) and prompt_count >= len(state["commands"]) + 1:
                runner.request_stop(0.2)

        runner.output_hooks.append(on_output)

    return monitor


def stop_breakpoint(name):
    def monitor(runner):
        triggered = {"done": False}

        def on_output():
            if triggered["done"]:
                return

            # NEMU 版不走 GDB 断点。对 util lab 里的 sys_sleep，
            # 这里在 shell 回显命令后立刻停止，等价地验证 sleep
            # 已经阻塞住后续的 echo FAIL。
            if name == "sys_sleep" and re.search(r"\$ sleep \d+", runner.qemu.output):
                triggered["done"] = True
                runner.request_stop(0.2)

        runner.output_hooks.append(on_output)

    return monitor


##################################################################
# Build helpers
#


def run_cmd(cmd, cwd=None):
    if OPTIONS and OPTIONS.verbose:
        print("$ " + " ".join(cmd))
    completed = subprocess.run(cmd, cwd=cwd)
    if completed.returncode != 0:
        sys.exit(completed.returncode)


def build_env():
    global BUILD_READY
    if BUILD_READY:
        return

    xv6_make("kernel/kernel", "fs.img")
    run_cmd([
        OBJCOPY, "-S", "-O", "binary",
        os.path.join(XV6_HOME, "kernel", "kernel"),
        os.path.join(XV6_HOME, "kernel", "kernel.bin"),
    ])
    run_cmd(["make", "-C", NEMU_HOME, "ARCH=native", "riscv32-xv6_defconfig"])
    run_cmd(["make", "-C", NEMU_HOME, "ISA=riscv32", "-j2"])
    BUILD_READY = True


def xv6_make(*targets):
    cmd = [
        "make", "-C", XV6_HOME, *targets,
        "CC=riscv64-linux-gnu-gcc -march=rv32ima_zicsr -mabi=ilp32",
        "TOOLPREFIX=riscv64-linux-gnu-",
        "CFLAGS=-Wall -Werror -O -fno-omit-frame-pointer -ggdb -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -march=rv32ima_zicsr -mabi=ilp32",
        "LDFLAGS=-melf32lriscv -z max-page-size=4096",
    ]
    env = dict(os.environ)
    env["CCACHE_DISABLE"] = CCACHE_DISABLE
    if OPTIONS and OPTIONS.verbose:
        print("$ " + " ".join(cmd))
    completed = subprocess.run(cmd, env=env)
    if completed.returncode != 0:
        sys.exit(completed.returncode)


def reset_fs():
    fs_image = os.path.join(XV6_HOME, "fs.img")
    if os.path.exists(fs_image):
        os.unlink(fs_image)
    xv6_make("fs.img")


##################################################################
# NEMU controller
#

__all__ += ["Runner"]


class NEMUProcess(object):
    def __init__(self):
        env = dict(os.environ)
        env["XV6_HOME"] = XV6_HOME
        env["SDL_VIDEODRIVER"] = SDL_VIDEODRIVER
        env["CCACHE_DISABLE"] = CCACHE_DISABLE
        self.proc = subprocess.Popen(
            [
                os.path.join(NEMU_HOME, "build", "riscv32-nemu-interpreter"),
                "-b",
                os.path.join(XV6_HOME, "kernel", "kernel.bin"),
            ],
            cwd=NEMU_HOME,
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.outbytes = bytearray()
        self.output = ""

    def read_once(self, timeout):
        ready, _, _ = select.select([self.proc.stdout], [], [], timeout)
        if not ready:
            return False
        data = os.read(self.proc.stdout.fileno(), 4096)
        if not data:
            return False
        self.outbytes.extend(data)
        self.output = self.outbytes.decode("utf-8", "replace")
        return True

    def write(self, text):
        if isinstance(text, str):
            text = text.encode("utf-8")
        self.proc.stdin.write(text)
        self.proc.stdin.flush()

    def terminate(self):
        if self.proc.poll() is None:
            self.proc.terminate()

    def wait(self, timeout=5):
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()


class Runner(object):
    def __init__(self, *default_monitors):
        self.default_monitors = default_monitors
        self.qemu = None
        self.output_hooks = []
        self.save_path = None
        self._stop_at = None

    def request_stop(self, delay=0.0):
        when = time.time() + delay
        if self._stop_at is None or when < self._stop_at:
            self._stop_at = when

    def run_qemu(self, *monitors, **kw):
        timeout = kw.get("timeout", 75)
        build_env()
        reset_fs()

        self.qemu = NEMUProcess()
        self.output_hooks = []
        self._stop_at = None

        for monitor in self.default_monitors + monitors:
            monitor(self)

        deadline = time.time() + timeout
        while True:
            if self.qemu.proc.poll() is not None:
                self.qemu.read_once(0)
                break

            if self._stop_at is not None and time.time() >= self._stop_at:
                break

            if time.time() >= deadline:
                self.qemu.terminate()
                self.qemu.wait()
                self._save_output()
                raise AssertionError("Timeout while waiting for xv6 output")

            got = self.qemu.read_once(0.2)
            if got:
                for hook in list(self.output_hooks):
                    hook()

        self.qemu.terminate()
        self.qemu.wait()
        self._save_output()

    def _save_output(self):
        if self.save_path:
            with open(self.save_path, "w") as output_file:
                output_file.write(self.qemu.output)

    def match(self, *regexps, **kw):
        assert_lines_match(self.qemu.output, *regexps, **kw)
