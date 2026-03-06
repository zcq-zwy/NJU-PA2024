/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <cpu/difftest.h>
#include <locale.h>

/* The assembly code of instructions executed is only output to the screen
 * when the number of instructions executed is less than this value.
 * This is useful when you use the `si' command.
 * You can modify this value as you want.
 */
#define MAX_INST_TO_PRINT 10

// iringbuf 的容量：保留最近 16 条指令日志。
#define IRINGBUF_SIZE 16
// 每条日志的最大长度，和 Decode.logbuf 对齐即可。
#define IRINGBUF_LINE_LEN 128

CPU_state cpu = {};
uint64_t g_nr_guest_inst = 0;
static uint64_t g_timer = 0; // unit: us
static bool g_print_step = false;

// 环形缓冲区本体：用于记录“最近执行过的指令字符串”。
static char iringbuf[IRINGBUF_SIZE][IRINGBUF_LINE_LEN];
// 下一个写入位置（始终指向可写槽位）。
static int iringbuf_head = 0;
// 当前已保存的有效条目数（<= IRINGBUF_SIZE）。
static int iringbuf_count = 0;
// 最近一次写入的槽位索引，用于输出时打箭头标记。
static int iringbuf_latest = -1;

void device_update();

// 监视点检查函数（在 sdb/watchpoint.c 中实现）
bool wp_check(void);

// 记录一条指令到 iringbuf。
// - 开启 ITRACE 时直接复用反汇编字符串；
// - 未开启 ITRACE 时退化为“pc + 原始机器码”的简要信息。
static void iringbuf_record(Decode *s) {
#ifdef CONFIG_ITRACE
  snprintf(iringbuf[iringbuf_head], IRINGBUF_LINE_LEN, "%s", s->logbuf);
#else
  snprintf(iringbuf[iringbuf_head], IRINGBUF_LINE_LEN,
      FMT_WORD ": %08x", s->pc, s->isa.inst);
#endif

  iringbuf_latest = iringbuf_head;
  iringbuf_head = (iringbuf_head + 1) % IRINGBUF_SIZE;
  if (iringbuf_count < IRINGBUF_SIZE) iringbuf_count++;
}

// 按“最老 -> 最新”的顺序打印 iringbuf，最新一条前缀为 "-->"。
static void iringbuf_display(void) {
  if (iringbuf_count == 0) {
    Log("iringbuf is empty");
    return;
  }

  Log("iringbuf (oldest -> newest):");
  int start = (iringbuf_head - iringbuf_count + IRINGBUF_SIZE) % IRINGBUF_SIZE;
  for (int i = 0; i < iringbuf_count; i++) {
    int idx = (start + i) % IRINGBUF_SIZE;
    Log("%s %s", (idx == iringbuf_latest ? "-->" : "   "), iringbuf[idx]);
  }
}

static void trace_and_difftest(Decode *_this, vaddr_t dnpc) {
#ifdef CONFIG_ITRACE_COND
  if (ITRACE_COND) { log_write("%s\n", _this->logbuf); }
#endif
  if (g_print_step) { IFDEF(CONFIG_ITRACE, puts(_this->logbuf)); }
  IFDEF(CONFIG_DIFFTEST, difftest_step(_this->pc, dnpc));

  // 只有在开启 CONFIG_WATCHPOINT 时才进行监视点检查：
  // 1) 关闭该配置时不引入额外运行时开销；
  // 2) 命中监视点后把状态切到 NEMU_STOP，交还控制权给 sdb。
  IFDEF(CONFIG_WATCHPOINT, {
      if (wp_check()) {
        // 监视点命中：暂停执行，返回 sdb 主循环等待用户指令。
        nemu_state.state = NEMU_STOP;
      }
    });
}

static void exec_once(Decode *s, vaddr_t pc) {
  s->pc = pc;
  s->snpc = pc;
  isa_exec_once(s);
  cpu.pc = s->dnpc;
#ifdef CONFIG_ITRACE
  char *p = s->logbuf;
  p += snprintf(p, sizeof(s->logbuf), FMT_WORD ":", s->pc);
  int ilen = s->snpc - s->pc;
  int i;
  uint8_t *inst = (uint8_t *)&s->isa.inst;
#ifdef CONFIG_ISA_x86
  for (i = 0; i < ilen; i ++) {
#else
  for (i = ilen - 1; i >= 0; i --) {
#endif
    p += snprintf(p, 4, " %02x", inst[i]);
  }
  int ilen_max = MUXDEF(CONFIG_ISA_x86, 8, 4);
  int space_len = ilen_max - ilen;
  if (space_len < 0) space_len = 0;
  space_len = space_len * 3 + 1;
  memset(p, ' ', space_len);
  p += space_len;

  void disassemble(char *str, int size, uint64_t pc, uint8_t *code, int nbyte);
  disassemble(p, s->logbuf + sizeof(s->logbuf) - p,
      MUXDEF(CONFIG_ISA_x86, s->snpc, s->pc), (uint8_t *)&s->isa.inst, ilen);
#endif
}

static void execute(uint64_t n) {
  Decode s;
  for (;n > 0; n --) {
    exec_once(&s, cpu.pc);
    // 每执行完一条指令就写入环形缓冲，保证异常时能回溯最近现场。
    iringbuf_record(&s);
    g_nr_guest_inst ++;
    trace_and_difftest(&s, cpu.pc);
    if (nemu_state.state != NEMU_RUNNING) break;
    IFDEF(CONFIG_DEVICE, device_update());
  }
}

static void statistic() {
  IFNDEF(CONFIG_TARGET_AM, setlocale(LC_NUMERIC, ""));
#define NUMBERIC_FMT MUXDEF(CONFIG_TARGET_AM, "%", "%'") PRIu64
  Log("host time spent = " NUMBERIC_FMT " us", g_timer);
  Log("total guest instructions = " NUMBERIC_FMT, g_nr_guest_inst);
  if (g_timer > 0) Log("simulation frequency = " NUMBERIC_FMT " inst/s", g_nr_guest_inst * 1000000 / g_timer);
  else Log("Finish running in less than 1 us and can not calculate the simulation frequency");
}

void assert_fail_msg() {
  // 断言失败时优先打印最近指令窗口，便于快速定位上下文。
  iringbuf_display();
  isa_reg_display();
  statistic();
}

/* Simulate how the CPU works. */
void cpu_exec(uint64_t n) {
  g_print_step = (n < MAX_INST_TO_PRINT);
  switch (nemu_state.state) {
    case NEMU_END: case NEMU_ABORT: case NEMU_QUIT:
      printf("Program execution has ended. To restart the program, exit NEMU and run again.\n");
      return;
    default: nemu_state.state = NEMU_RUNNING;
  }

  uint64_t timer_start = get_time();

  execute(n);

  uint64_t timer_end = get_time();
  g_timer += timer_end - timer_start;

  switch (nemu_state.state) {
    case NEMU_RUNNING: nemu_state.state = NEMU_STOP; break;

    case NEMU_END: case NEMU_ABORT: {
      // ABORT 或 BAD TRAP 都属于异常结束，先把最近指令打印出来。
      bool bad = (nemu_state.state == NEMU_ABORT || nemu_state.halt_ret != 0);
      if (bad) {
        iringbuf_display();
      }
      Log("nemu: %s at pc = " FMT_WORD,
          (nemu_state.state == NEMU_ABORT ? ANSI_FMT("ABORT", ANSI_FG_RED) :
           (nemu_state.halt_ret == 0 ? ANSI_FMT("HIT GOOD TRAP", ANSI_FG_GREEN) :
            ANSI_FMT("HIT BAD TRAP", ANSI_FG_RED))),
          nemu_state.halt_pc);
      // fall through
    }
    case NEMU_QUIT: statistic();
  }
}
