#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>
#include <stdbool.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

// 写入单个字符到输出缓冲。
// - n > 0 且仍有空间时，真实写入 out；
// - 无空间时只增加 idx，用于正确计算“理论输出长度”。
static void emit_char(char *out, size_t n, size_t *idx, char ch) {
  if (n > 0 && *idx + 1 < n) {
    out[*idx] = ch;
  }
  (*idx)++;
}

// 写入字符串到输出缓冲。
// 为了避免空指针崩溃，NULL 按 "(null)" 输出。
static void emit_str(char *out, size_t n, size_t *idx, const char *s) {
  if (s == NULL) s = "(null)";
  while (*s) emit_char(out, n, idx, *s++);
}

// 以指定进制输出无符号整数。
// 支持：
// - base: 10/16 等；
// - upper: 十六进制大小写；
// - width/pad: 最小宽度与填充字符（空格或 '0'）。
static void emit_uint(char *out, size_t n, size_t *idx, unsigned long long v,
                      unsigned base, bool upper, int width, char pad) {
  static const char *digits_l = "0123456789abcdef";
  static const char *digits_u = "0123456789ABCDEF";
  const char *digits = upper ? digits_u : digits_l;

  // 先逆序取各位数字，再反向输出为正序文本。
  char buf[32];
  int len = 0;
  do {
    buf[len++] = digits[v % base];
    v /= base;
  } while (v != 0);

  while (len < width) {
    emit_char(out, n, idx, pad);
    width--;
  }
  while (len > 0) {
    emit_char(out, n, idx, buf[--len]);
  }
}

// 核心格式化函数：实现截断语义与返回值语义。
// 返回值是“理论应输出长度”（不含结尾 '\0'），即使缓冲被截断也如此。
int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  size_t idx = 0;

  for (const char *p = fmt; *p; p++) {
    if (*p != '%') {
      emit_char(out, n, &idx, *p);
      continue;
    }

    p++;
    if (*p == '\0') break;

    char pad = ' ';
    int width = 0;

    // 解析可选前缀：'0' 填充 + 十进制最小宽度。
    if (*p == '0') {
      pad = '0';
      p++;
    }
    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      p++;
    }

    // 吞掉长度修饰符 l/ll（当前实现与 PA 测试需求匹配）。
    if (*p == 'l') {
      p++;
      if (*p == 'l') p++;
    }

    switch (*p) {
      case '%': emit_char(out, n, &idx, '%'); break;

      case 'c': {
        char c = (char)va_arg(ap, int);
        emit_char(out, n, &idx, c);
        break;
      }

      case 's': {
        const char *s = va_arg(ap, const char *);
        emit_str(out, n, &idx, s);
        break;
      }

      case 'd': {
        long long v = (long long)va_arg(ap, int);
        unsigned long long uv;
        if (v < 0) {
          emit_char(out, n, &idx, '-');
          // 避免对 INT_MIN 直接取负导致溢出。
          uv = (unsigned long long)(-(v + 1)) + 1;
        } else {
          uv = (unsigned long long)v;
        }
        emit_uint(out, n, &idx, uv, 10, false, width, pad);
        break;
      }

      case 'u': {
        unsigned v = va_arg(ap, unsigned);
        emit_uint(out, n, &idx, (unsigned long long)v, 10, false, width, pad);
        break;
      }

      case 'x': {
        unsigned v = va_arg(ap, unsigned);
        emit_uint(out, n, &idx, (unsigned long long)v, 16, false, width, pad);
        break;
      }

      case 'X': {
        unsigned v = va_arg(ap, unsigned);
        emit_uint(out, n, &idx, (unsigned long long)v, 16, true, width, pad);
        break;
      }

      case 'p': {
        uintptr_t v = (uintptr_t)va_arg(ap, void *);
        emit_str(out, n, &idx, "0x");
        emit_uint(out, n, &idx, (unsigned long long)v, 16, false,
                  (int)(sizeof(void *) * 2), '0');
        break;
      }

      default:
        // 不支持的格式符按字面输出，保证行为可预测，便于调试。
        emit_char(out, n, &idx, '%');
        emit_char(out, n, &idx, *p);
        break;
    }
  }

  // 只要 n > 0，就保证结果是 '\0' 结尾（snprintf 标准语义）。
  if (n > 0) {
    size_t pos = (idx < n - 1) ? idx : (n - 1);
    out[pos] = '\0';
  }

  return (int)idx;
}

// 可变参数封装：把 ... 转成 va_list 后复用 vsnprintf。
int snprintf(char *out, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return ret;
}

// 与标准库语义一致：视为“输出缓冲足够大”的格式化。
int vsprintf(char *out, const char *fmt, va_list ap) {
  return vsnprintf(out, (size_t)-1, fmt, ap);
}

// 与标准库语义一致：视为“输出缓冲足够大”的格式化。
int sprintf(char *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(out, (size_t)-1, fmt, ap);
  va_end(ap);
  return ret;
}

// printf 的最小实现：
// 1) 先格式化到临时缓冲；
// 2) 再逐字符调用 putch 输出到控制台。
int printf(const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  for (int i = 0; i < ret && i < (int)sizeof(buf) - 1; i++) {
    putch(buf[i]);
  }
  return ret;
}

#endif
