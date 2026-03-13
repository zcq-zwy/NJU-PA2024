#ifndef LOCKLAB_YEAR
#define LOCKLAB_YEAR 2025
#endif

#if LOCKLAB_YEAR == 2024
#include "../official-lock-tests/rv32/2024/user/bcachetest.c"
#elif LOCKLAB_YEAR == 2025
#include "../official-lock-tests/original/2025/user/bcachetest.c"
#else
#error "unsupported LOCKLAB_YEAR"
#endif
