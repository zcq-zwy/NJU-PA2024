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

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sdb.h"
#include <memory/vaddr.h>

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}


static int cmd_q(char *args) {
  set_nemu_state(NEMU_QUIT, cpu.pc, 0); // 标记为“用户主动退出”
  return -1; // 让 sdb_mainloop 结束
}

static int cmd_si(char *args) {
    // 未提供参数时，默认单步执行 1 条指令
    uint64_t n = 1;

    if (args != NULL) {
      char *end = NULL;
      n = strtoull(args, &end, 10);

      // 参数非法（非数字、带多余字符、或为 0）时给出用法提示
      if (end == args || (*end != '\0' && *end != '\n') || n == 0) {
        printf("Usage: si [N]\n");
        return 0;
      }
    }

    // 执行 N 条指令后返回到 sdb
    cpu_exec(n);
    return 0;
  }



  /**
  命令层 -> 功能层 -> ISA实现层”：

  1. cmd_info
     在 sdb.c，负责解析用户输入 info r / info w。
  2. wp_display
     在 watchpoint.c，负责打印监视点列表（info w 用）。
  3. isa_reg_display
     在 src/isa/$ISA/reg.c，负责打印当前 ISA 的寄存器（info r 用）。

  调用链：

  - info r -> cmd_info() -> isa_reg_display()
  - info w -> cmd_info() -> wp_display()
   */

   static int cmd_info(char *args) {
    // info 命令必须带子命令：r 或 w
    if (args == NULL) {
      printf("Usage: info r|w\n");
      return 0;
    }

    // info r: 打印寄存器状态（ISA 相关实现）
    if (strcmp(args, "r") == 0) {
      isa_reg_display();
    }
    // info w: 打印当前监视点列表
    else if (strcmp(args, "w") == 0) {
      wp_display();
    }
    // 其它子命令暂不支持
    else {
      printf("Unknown subcommand '%s'\n", args);
    }

    return 0;
  }


  static int cmd_x(char *args) {
    // 用法: x N 0xADDR
    if (args == NULL) {
      printf("Usage: x N 0xADDR\n");
      return 0;
    }

    // 直接用 strtok 拆两个参数，避免手算指针出错
    char *n_str = strtok(args, " ");
    char *addr_str = strtok(NULL, " ");
    if (n_str == NULL || addr_str == NULL) {
      printf("Usage: x N 0xADDR\n");
      return 0;
    }

    char *end_n = NULL;
    unsigned long n = strtoul(n_str, &end_n, 10);
    if (end_n == n_str || *end_n != '\0' || n == 0) {
      printf("Invalid N: %s\n", n_str);
      return 0;
    }

    // 简化版只接受十六进制地址
    if (!(addr_str[0] == '0' && (addr_str[1] == 'x' || addr_str[1] == 'X'))) {
      printf("Address must be hex like 0x80000000\n");
      return 0;
    }

    char *end_addr = NULL;
    vaddr_t addr = (vaddr_t)strtoul(addr_str, &end_addr, 16);
    if (end_addr == addr_str || *end_addr != '\0') {
      printf("Invalid address: %s\n", addr_str);
      return 0;
    }

    // 连续输出 N 个 4-byte
    for (unsigned long i = 0; i < n; i++) {
      vaddr_t cur = addr + i * 4;
      word_t data = vaddr_read(cur, 4);   // 若你想按物理地址读，改 paddr_read
      printf(FMT_WORD ": " FMT_WORD "\n", cur, data);
    }

    return 0;
  }

  static int cmd_p(char *args) {
    // p 命令必须带表达式参数
    if (args == NULL) {
      printf("Usage: p EXPR\n");
      return 0;
    }

    // 调用表达式求值
    bool success = true;
    word_t val = expr(args, &success);

    // 求值失败给出提示
    if (!success) {
      printf("Bad expression: %s\n", args);
      return 0;
    }

    // 按当前 ISA 位宽打印结果
    printf(FMT_WORD "\n", val);
    return 0;
  }

static int cmd_help(char *args);

static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si", "Step program by N instructions (default 1)", cmd_si },
  { "info", "Print program status: info r (registers), info w (watchpoints)", cmd_info },
  { "x", "Scan memory: x N 0xADDR", cmd_x },
  { "p", "Evaluate expression", cmd_p },

  /* TODO: Add more commands */

};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
