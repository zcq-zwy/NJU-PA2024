# xv6 on NEMU 交互输入问题定位与修复

## 1. 现象

在 `xv6-fs-lab` 分支上, xv6 已经可以启动到 shell:

```text
xv6 kernel is booting

init: starting sh
$
```

但手动输入命令时, 经常出现如下异常:

- 输入 `echo hi` 后停在 `$ echo hi`
- 回车、退格表现异常
- 有时输出会“斜着排版”
- 用脚本往 `stdin` 喂命令时, xv6 shell 看起来没有真正收到输入

这说明问题不在 `fs` 实现本身, 而在 **NEMU 的 xv6 UART 输入链路** 和 **外层脚本如何接管终端**。

---

## 2. 根因

关键代码在 `nemu/src/device/device.c` 的 `init_xv6_uart_stdin()`。

### 2.1 NEMU 默认更偏向读 `/dev/tty`

对于 xv6 UART, NEMU 并不是简单地只读 `stdin`, 它更适合直接从终端设备读取输入。

如果外层脚本再做这些事情:

- 用 `script` 套一层 PTY
- 强行改 `stty`
- 通过 `pipe/stdin` 自动喂命令

就很容易和 NEMU 自己的输入处理打架。

结果就是:

- 终端上“看见了字符”, 但不一定是 xv6 真收到了
- 回车可能只是宿主终端处理了, 没有正确送到 xv6
- shell 提示符看似正常, 但命令不执行

### 2.2 交互模式和自动化模式其实是两套不同需求

这里要区分两类场景:

#### 交互 shell

目标是手动输入命令。  
最稳的做法是:

- 让 NEMU 直接接管真实终端
- 让 NEMU 自己把终端切到 raw 模式
- 输入源使用 `/dev/tty`

#### 自动化脚本

目标是脚本自动发送命令。  
这时才应该:

- 明确让 NEMU 从 `stdin` 读取
- 由脚本通过 `pipe`/`stdin` 送入命令

如果把这两套方案混用, 就会得到前面的“能看到字符但命令不执行”的异常。

---

## 3. 修复思路

### 3.1 在 NEMU 中加入输入源切换开关

修改 `nemu/src/device/device.c`, 让 xv6 UART 输入支持环境变量:

- `NEMU_XV6_UART_INPUT=stdin`

其含义是:

- 如果设置为 `stdin`/`pipe`, 则 NEMU 使用标准输入作为 xv6 UART 输入源
- 否则默认优先尝试 `/dev/tty`

这样以后:

- **交互 shell** 继续走 `/dev/tty`
- **自动化脚本** 明确走 `stdin`

这一步解决了“自动化脚本往 `stdin` 发命令, 但 NEMU 实际上在读 `/dev/tty`”的问题。

### 3.2 交互 shell 采用“让 NEMU 自己管理终端”的策略

最终 `run_xv6_fs_shell.sh` 采用了和之前可工作的分支一致的思路:

```bash
stty sane || true

if [ -r /dev/tty ]; then
  exec env XV6_HOME="$XV6_HOME" SDL_VIDEODRIVER=dummy NEMU_XV6_UART_RAW=1 \
    ./build/riscv32-nemu-interpreter -l "$NEMU_LOG" -b "$XV6_HOME/kernel/kernel.bin" < /dev/tty
fi
```

关键点:

- 不再用 `script`
- 不再自己折腾 `stty raw`
- 不再强行改成 `stdin`
- 直接把 `/dev/tty` 交给 NEMU
- 用 `NEMU_XV6_UART_RAW=1` 告诉 NEMU: **你自己切 raw**

也就是:

- 真实终端 -> NEMU UART 输入 -> xv6 console -> shell

中间没有多余的外层脚本去劫持键盘和回显。

### 3.3 自动化命令脚本改成显式 `stdin` 模式

对于 `run_xv6_fs_cmd.sh`, 则显式设置:

```bash
NEMU_XV6_UART_INPUT=stdin
```

这样脚本往 `stdin` 写命令时, xv6 才会真正收到。

---

## 4. 为什么之前会“显示字符但不执行”

这是这次排错最容易误判的点。

看到:

```text
$ echo hi
```

并不自动意味着 xv6 shell 已经收到了完整命令。

可能发生的是:

- 宿主终端或 PTY 自己做了本地回显
- NEMU 没有按预期拿到输入
- xv6 其实还在等真正的换行和命令内容

所以判断是否真的成功, 不能只看“字符有没有显示出来”, 而要看:

- 是否真正出现 `hi`
- 是否重新返回 `$`

---

## 5. 最终稳定方案

### 5.1 进入交互 shell

使用:

```bash
cd /root/ics2024
./run_xv6_fs_shell.sh
```

这套方案适合:

- 手动输入 `echo hi`
- 手动输入 `ls`
- 手动输入 `symlinktest`

### 5.2 非交互自动执行命令

使用:

```bash
cd /root/ics2024
./run_xv6_fs_cmd.sh "echo hi"
./run_xv6_fs_cmd.sh "symlinktest"
NO_REBUILD=1 ./run_xv6_fs_cmd.sh "bigfile 268"
```

这套方案适合:

- 绕过终端交互不稳定
- 自动测试 xv6 命令
- 连续跑多个 fs 相关程序

---

## 6. 这次问题的教训

### 6.1 串口输入不是“只要有 stdin 就行”

模拟器里串口输入往往涉及:

- `stdin`
- `/dev/tty`
- raw/canonical 模式
- 非阻塞读
- 本地回显

这些概念一旦混在一起, 表面现象会非常迷惑。

### 6.2 交互模式和自动化模式必须分开设计

这是这次修复最核心的经验:

- **交互模式**: 让 NEMU 自己接管终端
- **自动化模式**: 明确让 NEMU 从 `stdin` 读

不要试图用同一套“外层强行控终端”的做法同时兼顾两种场景。

### 6.3 先确认“问题在哪一层”

这次如果不先区分:

- xv6 是否已经启动到 shell
- 键盘输入是否送达 NEMU
- NEMU 是否把字符喂给 xv6 UART

就很容易误以为是 `fs` 代码、`bigfile` 程序、甚至 shell 自己写错了。

实际上, 根因在 **模拟器输入路径**。

---

## 7. 一句话总结

这次 `echo hi` 卡住, 不是 xv6 shell 不会执行命令, 而是 **NEMU 的 xv6 UART 输入源选择和外层终端接管方式冲突了**。  
修复的关键是:

- 交互 shell: `/dev/tty` + `NEMU_XV6_UART_RAW=1`
- 自动脚本: `NEMU_XV6_UART_INPUT=stdin`

把“交互”和“自动化”彻底分开后, 输入链路就稳定了。
