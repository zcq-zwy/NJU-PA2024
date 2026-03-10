#include <proc.h>

#define MAX_NR_PROC 4

static PCB pcb[MAX_NR_PROC] __attribute__((used)) = {};
static PCB pcb_boot = {};
PCB *current = NULL;

static void __attribute__((unused)) context_kload(PCB *pcb, void (*entry)(void *), void *arg) {
  Area kstack = { .start = pcb->stack, .end = pcb->stack + STACK_SIZE };
  pcb->cp = kcontext(kstack, entry, arg);
}

void switch_boot_pcb() {
  current = &pcb_boot;
}

void hello_fun(void *arg) {
  const char *name = (const char *)arg;
  int times = 1;
  while (1) {
    if (times <= 5 || times % 100000 == 0) {
      Log("Hello World from Nanos-lite kernel thread %s for the %dth time!", name, times);
    }
    times ++;
    yield();
  }
}

void init_proc() {
  Log("Initializing processes...");

  static char *dummy_argv[] = { "/bin/dummy", NULL };
  int ret = context_uload(&pcb[0], "/bin/dummy", dummy_argv, NULL);
  assert(ret == 0);
  switch_boot_pcb();
}

Context* schedule(Context *prev) {
  if (current != NULL) {
    current->cp = prev;
  }

  current = &pcb[0];
  return current->cp;
}
