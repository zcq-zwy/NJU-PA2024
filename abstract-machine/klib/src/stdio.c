#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>
#include <stdbool.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)


static void emit_char(char *out, size_t n, size_t *idx, char ch) {
  if (n > 0 && *idx + 1 < n) {
    out[*idx] = ch;
  }
  (*idx)++;
}


static void emit_str(char *out, size_t n, size_t *idx, const char *s) {
  if (s == NULL) s = "(null)";
  while (*s) emit_char(out, n, idx, *s++);
}


static void emit_uint(char *out, size_t n, size_t *idx, unsigned long long v,
                      unsigned base, bool upper, int width, char pad) {
  static const char *digits_l = "0123456789abcdef";
  static const char *digits_u = "0123456789ABCDEF";
  const char *digits = upper ? digits_u : digits_l;

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

    // ????? 0 ????????
    if (*p == '0') {
      pad = '0';
      p++;
    }
    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      p++;
    }


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
        emit_uint(out, n, &idx, (unsigned long long)v, 16, false, (int)(sizeof(void *) * 2), '0');
        break;
      }
      default:
        emit_char(out, n, &idx, '%');
        emit_char(out, n, &idx, *p);
        break;
    }
  }

  if (n > 0) {
    size_t pos = (idx < n - 1) ? idx : (n - 1);
    out[pos] = '\0';
  }

  return (int)idx;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return ret;
}

int vsprintf(char *out, const char *fmt, va_list ap) {
  return vsnprintf(out, (size_t)-1, fmt, ap);
}

int sprintf(char *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(out, (size_t)-1, fmt, ap);
  va_end(ap);
  return ret;
}

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
