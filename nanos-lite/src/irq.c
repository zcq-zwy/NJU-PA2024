#include <common.h>
#include <proc.h>
#include "syscall.h"

void do_syscall(Context *c);

static Context* do_event(Event e, Context* c) {
  static int timer_irq_cnt = 0;
  switch (e.event) {
    case EVENT_IRQ_TIMER:
      if (timer_irq_cnt < 5) {
        Log("EVENT_IRQ_TIMER");
      }
      timer_irq_cnt ++;
      return schedule(c);
    case EVENT_YIELD:
      return schedule(c);
    case EVENT_SYSCALL:
      do_syscall(c);
      return c;
    default:
      panic("Unhandled event ID = %d", e.event);
  }
}

void init_irq(void) {
  Log("Initializing interrupt/exception handler...");
  cte_init(do_event);
}
