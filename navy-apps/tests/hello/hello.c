#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>

#define PRINT_INTERVAL_MS 3000

static unsigned long long now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (unsigned long long)tv.tv_sec * 1000ull + (unsigned long long)tv.tv_usec / 1000ull;
}

int main() {
  write(1, "Hello World!\n", 13);
  int i = 2;
  unsigned long long next_print = now_ms() + PRINT_INTERVAL_MS;
  while (1) {
    if (now_ms() >= next_print) {
      printf("Hello World from Navy-apps for the %dth time!\n", i ++);
      next_print += PRINT_INTERVAL_MS;
    }
  }
  return 0;
}
