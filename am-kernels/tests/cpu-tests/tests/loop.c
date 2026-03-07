  #include <am.h>
  #include <klib.h>
  #include <klib-macros.h>

  int main() {
    volatile int x = 0;
    while (1) { x++; }
    return 0;
  }
