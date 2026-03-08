#include <assert.h>
#include <stdio.h>
#include <fixedptc.h>

static void test_basic_ops(void) {
  assert(fixedpt_muli(fixedpt_rconst(1.5), 2) == fixedpt_rconst(3.0));
  assert(fixedpt_divi(fixedpt_rconst(3.0), 2) == fixedpt_rconst(1.5));
  assert(fixedpt_mul(fixedpt_rconst(1.5), fixedpt_rconst(-2.0)) == fixedpt_rconst(-3.0));
  assert(fixedpt_div(fixedpt_rconst(3.0), fixedpt_rconst(2.0)) == fixedpt_rconst(1.5));
  assert(fixedpt_abs(fixedpt_rconst(-1.25)) == fixedpt_rconst(1.25));
}

static void test_floor_ceil(void) {
  assert(fixedpt_floor(fixedpt_rconst(1.2)) == fixedpt_fromint(1));
  assert(fixedpt_ceil(fixedpt_rconst(1.2)) == fixedpt_fromint(2));
  assert(fixedpt_floor(fixedpt_rconst(-1.2)) == fixedpt_fromint(-2));
  assert(fixedpt_ceil(fixedpt_rconst(-1.2)) == fixedpt_fromint(-1));

  assert(fixedpt_floor(fixedpt_fromint(3)) == fixedpt_fromint(3));
  assert(fixedpt_ceil(fixedpt_fromint(3)) == fixedpt_fromint(3));
  assert(fixedpt_floor(fixedpt_fromint(-3)) == fixedpt_fromint(-3));
  assert(fixedpt_ceil(fixedpt_fromint(-3)) == fixedpt_fromint(-3));

  assert(fixedpt_floor(fixedpt_rconst(-0.1)) == fixedpt_fromint(-1));
  assert(fixedpt_ceil(fixedpt_rconst(-0.1)) == fixedpt_fromint(0));
}

int main(void) {
  test_basic_ops();
  test_floor_ceil();
  puts("fixedpt-test: PASS");
  return 0;
}
