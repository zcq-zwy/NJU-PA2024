/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <common.h>

void init_monitor(int, char *[]);
void am_init_monitor();
void engine_start();
int is_exit_status_bad();

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
word_t expr(char *e, bool *success);  // 临时声明即可


static void expr_batch_test(void) {
    FILE *fp = fopen("tools/gen-expr/input", "r");
    assert(fp != NULL);

    uint32_t expected = 0;
    char e[65536];
    uint64_t passed = 0;   // 通过条数计数

    while (fscanf(fp, "%" SCNu32 " ", &expected) == 1) {
      if (fgets(e, sizeof(e), fp) == NULL) break;

      size_t len = strlen(e);
      if (len > 0 && e[len - 1] == '\n') e[len - 1] = '\0';

      bool success = true;
      word_t got = expr(e, &success);

      if (!success || (uint32_t)got != expected) {
        printf("Mismatch: expr=\"%s\" expect=%u got=%u success=%d\n",
               e, expected, (uint32_t)got, success);
        assert(0);
      }

      passed++;
    }

    fclose(fp);
    printf("[expr-test] passed = %" PRIu64 "\n", passed);
  }

  

int main(int argc, char *argv[]) {
  /* Initialize the monitor. */
#ifdef CONFIG_TARGET_AM
  am_init_monitor();
#else
  init_monitor(argc, argv);
  printf("[expr-test] start\n");
  expr_batch_test();
  printf("[expr-test] done\n");
#endif

  /* Start engine. */
  engine_start();

  return is_exit_status_bad();
}
