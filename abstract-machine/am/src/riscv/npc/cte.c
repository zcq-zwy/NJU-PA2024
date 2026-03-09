#include <am.h>
#include <riscv/riscv.h>
#include <klib.h>
#include <klib-macros.h>

static Context* (*user_handler)(Event, Context*) = NULL;

Context* __am_irq_handle(Context *c) {
  if (user_handler) {
    Event ev = {0};
    switch (c->mcause) {
      case 11:
        c->mepc += 4;
        ev.event = (c->GPR1 == (uintptr_t)-1 ? EVENT_YIELD : EVENT_SYSCALL);
        break;
      default:
        ev.event = EVENT_ERROR;
        ev.cause = c->mcause;
        break;
    }

    c = user_handler(ev, c);
    assert(c != NULL);
  }

  return c;
}

extern void __am_asm_trap(void);
extern void __am_kcontext_start(void);

void __am_panic_on_return() { panic("kernel context returns"); }

bool cte_init(Context*(*handler)(Event, Context*)) {
  // initialize exception entry
  asm volatile("csrw mtvec, %0" : : "r"(__am_asm_trap));

  // register event handler
  user_handler = handler;

  return true;
}

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  uintptr_t sp = (uintptr_t)kstack.end;
  sp &= ~(uintptr_t)0xf;
  Context *c = (Context *)(sp - sizeof(Context));
  *c = (Context) { 0 };

  c->mstatus = 0x1800;
  c->mepc = (uintptr_t)__am_kcontext_start;
  c->gpr[2] = sp;
  c->GPR2 = (uintptr_t)arg;
  c->GPR3 = (uintptr_t)entry;
  c->pdir = NULL;

  return c;
}

void yield() {
#ifdef __riscv_e
  asm volatile("li a5, -1; ecall");
#else
  asm volatile("li a7, -1; ecall");
#endif
}

bool ienabled() {
  return false;
}

void iset(bool enable) {
}
