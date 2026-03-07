#include <monitor/etrace.h>
#include <utils.h>

#ifdef CONFIG_ETRACE

void etrace_log(word_t no, vaddr_t epc, vaddr_t target) {
  log_write("etrace: NO=" FMT_WORD ", epc=" FMT_WORD ", target=" FMT_WORD "\n",
      no, epc, target);
}

#else

void etrace_log(word_t no, vaddr_t epc, vaddr_t target) {
  (void)no;
  (void)epc;
  (void)target;
}

#endif
