#include <fcntl.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#define REPORT_INTERVAL_MS 3000

static unsigned long long now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (unsigned long long)tv.tv_sec * 1000ull + (unsigned long long)tv.tv_usec / 1000ull;
}

int main(void) {
  int fd = open("/proc/sysinfo", O_RDONLY);
  if (fd < 0) {
    printf("open /proc/sysinfo failed: %d\n", fd);
    return 1;
  }

  unsigned long long next = now_ms();
  char buf[256];

  while (1) {
    if (now_ms() >= next) {
      lseek(fd, 0, SEEK_SET);
      int n = read(fd, buf, sizeof(buf) - 1);
      if (n > 0) {
        buf[n] = '\0';
        for (int i = 0; i < n; i++) {
          if (buf[i] == '\n') buf[i] = ' ';
        }
        printf("[sysmon] %s\n", buf);
      }
      next += REPORT_INTERVAL_MS;
    }
  }

  return 0;
}
