#include <am.h>

#include <stdlib.h>
#include <unistd.h>

Area heap = {};

void putch(char ch) {
  write(STDOUT_FILENO, &ch, 1);
}

void halt(int code) {
  _exit(code);
}
