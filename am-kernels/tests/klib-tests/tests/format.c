#include "trap.h"
#include <limits.h>

#define BUF_SZ 256

// 校验 sprintf 的两类契约：
// 1) 返回值等于写入字符数（不含结尾 \0）；
// 2) 输出字符串内容与期望一致。
static void check_sprintf(const char *expect, const char *fmt, ...) {
  char buf[BUF_SZ];
  va_list ap;
  va_start(ap, fmt);
  int ret = vsprintf(buf, fmt, ap);
  va_end(ap);

  check(ret == (int)strlen(expect));
  check(strcmp(buf, expect) == 0);
}

static void test_int_formats(void) {
  // 题目给出的代表性边界数据，覆盖 0、最大最小值、符号位边界。
  check_sprintf("0", "%d", 0);
  check_sprintf("126322567", "%d", INT_MAX / 17);
  check_sprintf("2147483647", "%d", INT_MAX);
  check_sprintf("-2147483648", "%d", INT_MIN);
  check_sprintf("-2147483647", "%d", INT_MIN + 1);

  check_sprintf("252645135", "%u", UINT_MAX / 17u);
  check_sprintf("4294967295", "%u", UINT_MAX);

  check_sprintf("0", "%x", 0u);
  check_sprintf("ffffffff", "%x", UINT_MAX);
  check_sprintf("FFFFFFFF", "%X", UINT_MAX);
}

static void test_width_and_padding(void) {
  // 当前 klib 的实现支持最小宽度和 0 填充。
  check_sprintf("   42", "%5d", 42);
  check_sprintf("00042", "%05d", 42);
  check_sprintf("0002a", "%05x", 42u);
  check_sprintf("  2A", "%4X", 42u);

}

static void test_misc_formats(void) {
  check_sprintf("A", "%c", 65);
  check_sprintf("hello", "%s", "hello");
  check_sprintf("(null)", "%s", (char *)NULL);
  check_sprintf("100% done", "100%% done");

}

static void test_snprintf_semantics(void) {
  char buf[8];

  // n=0: 不写缓冲区，但返回理论长度。
  int ret0 = snprintf(buf, 0, "%d-%s", 12345, "abc");
  check(ret0 == 9);

  // 截断语义：最多写 n-1 个字符，并保证 \0 结尾。
  memset(buf, 35, sizeof(buf));
  int ret1 = snprintf(buf, sizeof(buf), "%d-%s", 12345, "abcdef");
  check(ret1 == 12);                    // 理论完整长度
  check(strcmp(buf, "12345-a") == 0); // 实际写入 7 字符 + \0
  check(buf[7] == 0);
}

int main(void) {
  test_int_formats();
  test_width_and_padding();
  test_misc_formats();
  test_snprintf_semantics();
  return 0;
}
