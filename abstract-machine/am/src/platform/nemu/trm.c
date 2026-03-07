#include <am.h>
#include <nemu.h>

extern char _heap_start;
int main(const char *args);
__attribute__((weak)) void fce_print_fps_summary(void);

Area heap = RANGE(&_heap_start, PMEM_END);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

void putch(char ch) {
  outb(SERIAL_PORT, ch);
}

void halt(int code) {
  if (fce_print_fps_summary) {
    fce_print_fps_summary();
  }
  nemu_trap(code);

  // should not reach here
  while (1);
}

void _trm_init() {
  int ret = main(mainargs);
  halt(ret);
}
