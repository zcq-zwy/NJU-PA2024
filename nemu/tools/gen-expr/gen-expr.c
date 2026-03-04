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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

// this should be enough
static char buf[65536] = {};
static char code_buf[65536 + 128] = {}; // a little larger than `buf`
static char *code_format =
"#include <stdio.h>\n"
"int main() { "
"  unsigned result = %s; "
"  printf(\"%%u\", result); "
"  return 0; "
"}";

// 当前写入到 buf 的偏移位置（始终指向字符串末尾）
static int buf_pos = 0;
// 返回 [0, n) 的随机数，用于控制分支/运算符/空格数量
  static uint32_t choose(uint32_t n) {
    return rand() % n;
  }

  // 安全写入工具：把格式化字符串追加到 buf 末尾
  // 返回 true 表示写入成功；false 表示空间不足（可视为“本次表达式作废”）
  static bool appendf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    // 只允许在剩余空间中写，避免越界
    int n = vsnprintf(buf + buf_pos, sizeof(buf) - buf_pos, fmt, ap);
    va_end(ap);

    // n < 0: 格式化失败；buf_pos + n 超界: 缓冲区不足
    if (n < 0 || buf_pos + n >= (int)sizeof(buf)) {
      return false;
    }

    // 更新写指针，保持 buf 始终是合法 C 字符串
    buf_pos += n;
    return true;
  }

  // 在 token 之间随机插入 0~2 个空格
  // 这样能测试词法分析是否正确忽略空白
  static void gen_space() {
    int n = choose(3);
    while (n--) appendf(" ");
  }

  // 生成一个无符号常量（加 u 后缀），确保 C 语义是无符号运算
  static void gen_num() {
    uint32_t x = (uint32_t)rand();
    appendf("%uu", x);
  }

  // 生成“保证非 0”的无符号常量，用于除号右操作数，减少除 0 样例
  static void gen_nonzero_num() {
    uint32_t x = (uint32_t)rand();
    if (x == 0) x = 1;
    appendf("%uu", x);
  }

  // 递归生成表达式：
  // depth 用于限制递归深度，防止表达式过长/栈过深
  static void gen_rand_expr_impl(int depth) {
    // 两个“收敛条件”：
    // 1) 递归太深，直接收敛成数字
    // 2) 缓冲区剩余空间不足，直接收敛成数字
    if (depth > 8 || buf_pos > (int)sizeof(buf) - 64) {
      gen_num();
      return;
    }

    // 三种生成形态：
    // 0: 纯数字
    // 1: 括号包裹子表达式
    // 2: 二元表达式 expr op expr
    switch (choose(3)) {
      case 0:
        gen_num();
        break;

      case 1:
        appendf("(");
        gen_space();
        gen_rand_expr_impl(depth + 1);
        gen_space();
        appendf(")");
        break;

      default: {
        // 左子表达式
        gen_rand_expr_impl(depth + 1);

        // 随机运算符前后可有空格
        gen_space();
        int op = choose(4);  // 0:+ 1:- 2:* 3:/
        if (op == 0) appendf("+");
        else if (op == 1) appendf("-");
        else if (op == 2) appendf("*");
        else appendf("/");
        gen_space();

        // 右子表达式：
        // 若是除法，右侧用“非零常量”规避除0
        if (op == 3) gen_nonzero_num();
        else gen_rand_expr_impl(depth + 1);

        break;
      }
    }
  }

// 对外入口：每次生成前先清空缓冲区并重置写指针
  static void gen_rand_expr() {
    buf_pos = 0;
    buf[0] = '\0';
    gen_rand_expr_impl(0);
  }

int main(int argc, char *argv[]) {
  int seed = time(0);
  srand(seed);
  int loop = 1;
  if (argc > 1) {
    sscanf(argv[1], "%d", &loop);
  }
  int i;
  for (i = 0; i < loop; i ++) {
    gen_rand_expr();

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -o /tmp/.expr");
    if (ret != 0) continue;

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);
  // main 中读取 oracle 结果后，建议加运行结果过滤：
  // 1) fscanf 没读到结果（ret != 1）
  // 2) 子进程退出异常（status != 0）
  // 这两类样例都丢弃，避免污染测试集
  unsigned result;
  ret = fscanf(fp, "%u", &result);
  int status = pclose(fp);
  if (ret != 1 || status != 0) continue;

  printf("%u %s\n", result, buf);
  }
  return 0;
}
