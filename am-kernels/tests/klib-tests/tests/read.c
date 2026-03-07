#include "trap.h"

#define N 64

static unsigned char buf1[N];
static unsigned char buf2[N];

// 参考实现：逐字节比较，返回首个不同字节的差值。
static int ref_memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *p = (const unsigned char *)a;
  const unsigned char *q = (const unsigned char *)b;
  for (size_t i = 0; i < n; i++) {
    if (p[i] != q[i]) return (int)p[i] - (int)q[i];
  }
  return 0;
}

// 参考实现：扫描到首个 '\0' 为止。
static size_t ref_strlen(const char *s) {
  size_t n = 0;
  while (s[n] != '\0') n++;
  return n;
}

static int ref_strcmp(const char *s1, const char *s2) {
  while (*s1 != '\0' && *s1 == *s2) {
    s1++;
    s2++;
  }
  return (unsigned char)*s1 - (unsigned char)*s2;
}

static int ref_strncmp(const char *s1, const char *s2, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char c1 = (unsigned char)s1[i];
    unsigned char c2 = (unsigned char)s2[i];
    if (c1 != c2) return (int)c1 - (int)c2;
    if (c1 == '\0') return 0;
  }
  return 0;
}

static void reset_mem(void) {
  for (int i = 0; i < N; i++) {
    buf1[i] = (unsigned char)((i * 17 + 3) & 0xff);
    buf2[i] = buf1[i];
  }
}

static void test_memcmp(void) {
  // 全等区间
  for (int n = 0; n <= N; n++) {
    reset_mem();
    int got = memcmp(buf1, buf2, n);
    int exp = ref_memcmp(buf1, buf2, n);
    check(got == exp);
  }

  // 单点差异 + 不同比较长度
  for (int pos = 0; pos < N; pos++) {
    for (int n = 0; n <= N; n++) {
      reset_mem();
      buf2[pos] ^= 0x5a;
      int got = memcmp(buf1, buf2, n);
      int exp = ref_memcmp(buf1, buf2, n);
      check(got == exp);
    }
  }
}

static void test_strlen(void) {
  char s[N];

  // 构造不同长度：在 len 处放 '\0'，后面填垃圾，验证不会越界读。
  for (int len = 0; len < N - 1; len++) {
    for (int i = 0; i < N; i++) s[i] = '#';
    for (int i = 0; i < len; i++) s[i] = (char)('a' + (i % 26));
    s[len] = '\0';
    for (int i = len + 1; i < N; i++) s[i] = (char)('A' + (i % 26));

    check(strlen(s) == ref_strlen(s));
  }
}

static void fill_string(char *s, int len, int seed) {
  for (int i = 0; i < len; i++) {
    s[i] = (char)('!' + ((i * 11 + seed) % 90));
  }
  s[len] = '\0';
}

static void test_strcmp(void) {
  char a[N], b[N];

  // 全等 + 不同长度 + 不同首差异位置
  for (int la = 0; la < 20; la++) {
    for (int lb = 0; lb < 20; lb++) {
      fill_string(a, la, 3);
      fill_string(b, lb, 3);
      check(strcmp(a, b) == ref_strcmp(a, b));

      int m = (la < lb ? la : lb);
      for (int pos = 0; pos < m; pos++) {
        fill_string(a, la, 5);
        fill_string(b, lb, 5);
        b[pos] = (char)(b[pos] + 1);
        check(strcmp(a, b) == ref_strcmp(a, b));
      }
    }
  }
}

static void test_strncmp(void) {
  char a[N], b[N];

  for (int la = 0; la < 20; la++) {
    for (int lb = 0; lb < 20; lb++) {
      fill_string(a, la, 7);
      fill_string(b, lb, 7);

      for (int n = 0; n <= 24; n++) {
        check(strncmp(a, b, (size_t)n) == ref_strncmp(a, b, (size_t)n));
      }

      int m = (la < lb ? la : lb);
      for (int pos = 0; pos < m; pos++) {
        fill_string(a, la, 9);
        fill_string(b, lb, 9);
        b[pos] = (char)(b[pos] - 1);
        for (int n = 0; n <= 24; n++) {
          check(strncmp(a, b, (size_t)n) == ref_strncmp(a, b, (size_t)n));
        }
      }
    }
  }
}

int main(void) {
  test_memcmp();
  test_strlen();
  test_strcmp();
  test_strncmp();
  return 0;
}
