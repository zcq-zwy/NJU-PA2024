#include <stdio.h>
#include <NDL.h>

int main() {
  NDL_Init(0);

  uint32_t next = NDL_GetTicks() + 500;

  for (int i = 1; i <= 5; i++) {
    while (NDL_GetTicks() < next) {
    }
    printf("timer-test: %d * 0.5s elapsed\n", i);
    next += 500;
  }

  NDL_Quit();
  return 0;
}
