#include "trap.h"

#define N 32

static uint8_t data[N];

static void reset_data(void) {
  for (int i = 0; i < N; i++) data[i] = (uint8_t)(i + 1);
}

static void check_seq(int l, int r, int val) {
  for (int i = l; i < r; i++) {
    check(data[i] == (uint8_t)(val + i - l));
  }
}

static void check_eq(int l, int r, int val) {
  for (int i = l; i < r; i++) {
    check(data[i] == (uint8_t)val);
  }
}

static void check_bytes(const uint8_t *buf, const uint8_t *exp, int n) {
  for (int i = 0; i < n; i++) check(buf[i] == exp[i]);
}

static void test_memset(void) {
  for (int l = 0; l < N; l++) {
    for (int r = l; r <= N; r++) {
      reset_data();
      uint8_t val = (uint8_t)((l * 7 + r * 13) & 0xff);
      memset(data + l, val, r - l);
      check_seq(0, l, 1);
      check_eq(l, r, val);
      check_seq(r, N, r + 1);
    }
  }
}

static void test_memcpy(void) {
  uint8_t src[N];
  for (int i = 0; i < N; i++) src[i] = (uint8_t)(0xa0 + i);

  for (int l = 0; l < N; l++) {
    for (int r = l; r <= N; r++) {
      reset_data();
      memcpy(data + l, src + l, r - l);
      check_seq(0, l, 1);
      for (int i = l; i < r; i++) check(data[i] == src[i]);
      check_seq(r, N, r + 1);
    }
  }
}

static void test_memmove(void) {
  uint8_t before[N];
  uint8_t expect[N];

  for (int dst = 0; dst < N; dst++) {
    for (int src = 0; src < N; src++) {
      int maxn = N - (dst > src ? dst : src);
      for (int n = 0; n <= maxn; n++) {
        reset_data();
        memcpy(before, data, N);
        memcpy(expect, before, N);

        for (int i = 0; i < n; i++) {
          expect[dst + i] = before[src + i];
        }

        memmove(data + dst, data + src, n);
        check_bytes(data, expect, N);
      }
    }
  }
}

static void init_buf(char *buf, int n, char c) {
  for (int i = 0; i < n; i++) buf[i] = c;
}

static void test_strcpy(void) {
  static const char *cases[] = {"", "a", "ab", "hello", "0123456789abcdef"};
  char buf[64];

  for (int k = 0; k < (int)(sizeof(cases) / sizeof(cases[0])); k++) {
    const char *s = cases[k];
    int len = strlen(s);
    for (int off = 0; off < 16; off++) {
      init_buf(buf, 64, '#');
      char *ret = strcpy(buf + off, s);
      check(ret == buf + off);

      for (int i = 0; i < off; i++) check(buf[i] == '#');
      for (int i = 0; i < len; i++) check(buf[off + i] == s[i]);
      check(buf[off + len] == '\0');
      for (int i = off + len + 1; i < 64; i++) check(buf[i] == '#');
    }
  }
}

static void test_strncpy(void) {
  static const char *cases[] = {"", "a", "abc", "hello", "abcdefghijklmnop"};
  char buf[64];

  for (int k = 0; k < (int)(sizeof(cases) / sizeof(cases[0])); k++) {
    const char *s = cases[k];
    int len = strlen(s);
    for (int n = 0; n <= 20; n++) {
      init_buf(buf, 64, '#');
      char *ret = strncpy(buf + 8, s, n);
      check(ret == buf + 8);

      for (int i = 0; i < 8; i++) check(buf[i] == '#');

      int copied = (len < n ? len : n);
      for (int i = 0; i < copied; i++) check(buf[8 + i] == s[i]);
      if (n > len) {
        for (int i = len; i < n; i++) check(buf[8 + i] == '\0');
      }

      for (int i = 8 + n; i < 64; i++) check(buf[i] == '#');
    }
  }
}

static void test_strcat(void) {
  static const char *lefts[] = {"", "A", "hello", "abcde"};
  static const char *rights[] = {"", "B", " world", "XYZ"};
  char buf[64];
  char exp[64];

  for (int i = 0; i < (int)(sizeof(lefts) / sizeof(lefts[0])); i++) {
    for (int j = 0; j < (int)(sizeof(rights) / sizeof(rights[0])); j++) {
      init_buf(buf, 64, '#');
      init_buf(exp, 64, 0);

      strcpy(buf + 4, lefts[i]);
      strcpy(exp, lefts[i]);
      strcat(exp, rights[j]);

      char *ret = strcat(buf + 4, rights[j]);
      check(ret == buf + 4);

      for (int k = 0; k < 4; k++) check(buf[k] == '#');
      check(strcmp(buf + 4, exp) == 0);

      int tail = 4 + strlen(exp) + 1;
      for (int k = tail; k < 64; k++) check(buf[k] == '#');
    }
  }
}

int main(void) {
  test_memset();
  test_memcpy();
  test_memmove();
  test_strcpy();
  test_strncpy();
  test_strcat();
  return 0;
}
