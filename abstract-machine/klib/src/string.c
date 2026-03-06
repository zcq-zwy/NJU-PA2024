#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

// 计算 C 字符串长度：从起始地址向后扫描，直到遇到 '\0' 为止。
// 返回值不包含结尾的 '\0' 本身。
size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n] != '\0') n++;
  return n;
}

// 复制字符串：把 src（含结尾 '\0'）完整复制到 dst。
// 返回 dst 的起始地址，便于链式调用。
char *strcpy(char *dst, const char *src) {
  char *ret = dst;
  while ((*dst++ = *src++) != '\0');
  return ret;
}

// 限长复制：最多复制 n 个字节。
// - 若 src 长度 < n，则后续位置补 '\0'；
// - 若 src 长度 >= n，则不会额外写入结尾 '\0'。
char *strncpy(char *dst, const char *src, size_t n) {
  char *ret = dst;
  size_t i = 0;
  for (; i < n && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  for (; i < n; i++) {
    dst[i] = '\0';
  }
  return ret;
}

// 字符串拼接：先找到 dst 的末尾 '\0'，再把 src（含 '\0'）接到后面。
char *strcat(char *dst, const char *src) {
  char *ret = dst;
  while (*dst != '\0') dst++;
  while ((*dst++ = *src++) != '\0');
  return ret;
}

// 字典序比较：按 unsigned char 逐字节比较，直到不等或遇到 '\0'。
// 返回值 <0 / =0 / >0 分别表示 s1 < / = / > s2。
int strcmp(const char *s1, const char *s2) {
  while (*s1 != '\0' && *s1 == *s2) {
    s1++;
    s2++;
  }
  return (unsigned char)*s1 - (unsigned char)*s2;
}

// 限长比较：最多比较 n 个字节。
// 在 n 范围内若提前遇到 '\0' 且此前都相等，返回 0。
int strncmp(const char *s1, const char *s2, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char c1 = (unsigned char)s1[i];
    unsigned char c2 = (unsigned char)s2[i];
    if (c1 != c2) return c1 - c2;
    if (c1 == '\0') return 0;
  }
  return 0;
}

// 内存填充：把缓冲区 s 的前 n 个字节都设为 c 的低 8 位。
void *memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  unsigned char v = (unsigned char)c;
  for (size_t i = 0; i < n; i++) {
    p[i] = v;
  }
  return s;
}

// 重叠安全拷贝：源和目的区间可能重叠时也能得到正确结果。
// - 无重叠/前向安全：从前往后拷贝；
// - 有重叠且 dst 落在 src 区间内：从后往前拷贝，避免覆盖未读源数据。
void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  if (d == s || n == 0) return dst;

  if (d < s || d >= s + n) {
    for (size_t i = 0; i < n; i++) d[i] = s[i];
  } else {
    for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
  }
  return dst;
}

// 原始字节拷贝：要求源区间与目的区间不重叠。
// 若重叠，行为未定义（UB），应改用 memmove。
void *memcpy(void *out, const void *in, size_t n) {
  unsigned char *dst = (unsigned char *)out;
  const unsigned char *src = (const unsigned char *)in;
  for (size_t i = 0; i < n; i++) {
    dst[i] = src[i];
  }
  return out;
}

// 内存比较：按 unsigned char 逐字节比较前 n 个字节。
// 返回首个不同字节的差值；若全相等返回 0。
int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  for (size_t i = 0; i < n; i++) {
    if (p1[i] != p2[i]) return p1[i] - p2[i];
  }
  return 0;
}

#endif
