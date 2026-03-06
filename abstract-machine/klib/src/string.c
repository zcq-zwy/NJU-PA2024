#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

// Count bytes before the first '\0'.
size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n] != '\0') n++;
  return n;
}

// Copy a C-string including terminating '\0'.
char *strcpy(char *dst, const char *src) {
  char *ret = dst;
  while ((*dst++ = *src++) != '\0');
  return ret;
}

// Copy at most n bytes; pad with '\0' if src is shorter.
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

// Append src to the end of dst and keep a trailing '\0'.
char *strcat(char *dst, const char *src) {
  char *ret = dst;
  while (*dst != '\0') dst++;
  while ((*dst++ = *src++) != '\0');
  return ret;
}

// Lexicographic compare in unsigned-char order.
int strcmp(const char *s1, const char *s2) {
  while (*s1 != '\0' && *s1 == *s2) {
    s1++;
    s2++;
  }
  return (unsigned char)*s1 - (unsigned char)*s2;
}

// Compare at most n bytes, stop early on '\0'.
int strncmp(const char *s1, const char *s2, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char c1 = (unsigned char)s1[i];
    unsigned char c2 = (unsigned char)s2[i];
    if (c1 != c2) return c1 - c2;
    if (c1 == '\0') return 0;
  }
  return 0;
}

// Fill n bytes with low 8 bits of c.
void *memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  unsigned char v = (unsigned char)c;
  for (size_t i = 0; i < n; i++) {
    p[i] = v;
  }
  return s;
}

// Overlap-safe copy: copy direction depends on address relationship.
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

// Raw byte copy; behavior is undefined for overlapping ranges.
void *memcpy(void *out, const void *in, size_t n) {
  unsigned char *dst = (unsigned char *)out;
  const unsigned char *src = (const unsigned char *)in;
  for (size_t i = 0; i < n; i++) {
    dst[i] = src[i];
  }
  return out;
}

// Byte-wise comparison in unsigned-char order.
int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  for (size_t i = 0; i < n; i++) {
    if (p1[i] != p2[i]) return p1[i] - p2[i];
  }
  return 0;
}

#endif
