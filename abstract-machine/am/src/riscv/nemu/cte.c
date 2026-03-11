#include <am.h>
#include <riscv/riscv.h>
#include <klib.h>
#include <klib-macros.h>

#define MSTATUS_MPP_M (3u << 11)
#define MSTATUS_MPIE  (1u << 7)
#define IRQ_TIMER     0x80000007u
#define CONTEXT_KERN  0u

static Context* (*user_handler)(Event, Context*) = NULL;

void __am_get_cur_as(Context *c);
void __am_switch(Context *c);

Context* __am_irq_handle(Context *c) {
  if (user_handler) {
    __am_get_cur_as(c);

    Event ev = {0};
    switch (c->mcause) {
      case IRQ_TIMER:
        ev.event = EVENT_IRQ_TIMER;
        break;
      case 8:
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
    __am_switch(c);
  }

  return c;
}

extern void __am_asm_trap(void);
extern void __am_kcontext_start(void);

void __am_panic_on_return() { panic("kernel context returns"); }

bool cte_init(Context*(*handler)(Event, Context*)) {
  // initialize exception entry
  asm volatile("csrw mtvec, %0" : : "r"(__am_asm_trap));
  asm volatile("csrw mscratch, zero");

  // register event handler
  user_handler = handler;

  return true;
}

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  uintptr_t sp = (uintptr_t)kstack.end;
  sp &= ~(uintptr_t)0xf;
  Context *c = (Context *)(sp - sizeof(Context));
  *c = (Context) { 0 };

  c->mstatus = MSTATUS_MPP_M | MSTATUS_MPIE;
  c->mepc = (uintptr_t)__am_kcontext_start;
  c->gpr[2] = sp;
  c->GPR2 = (uintptr_t)arg;
  c->GPR3 = (uintptr_t)entry;
  c->pdir = NULL;
  c->np = CONTEXT_KERN;

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
  uintptr_t mstatus = 0;
  asm volatile("csrr %0, mstatus" : "=r"(mstatus));
  return (mstatus & (1u << 3)) != 0;
}

void iset(bool enable) {
  if (enable) {
    asm volatile("csrsi mstatus, 8");
  } else {
    asm volatile("csrci mstatus, 8");
  }
}
