#include <proc.h>

#define MAX_NR_PROC 4
#define PAL_SLICES   5
#define HELLO_SLICES 1

static PCB pcb[MAX_NR_PROC] __attribute__((used)) = {};
static PCB pcb_boot = {};
PCB *current = NULL;
static int nr_proc = 0;
static int slices_left = 0;

static int proc_slices[MAX_NR_PROC] = {
  [0] = PAL_SLICES,
  [1] = HELLO_SLICES,
};

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

  static char *pal_argv[] = { "/bin/pal", NULL };
  int ret = context_uload(&pcb[0], "/bin/pal", pal_argv, NULL);
  assert(ret == 0);
  context_kload(&pcb[1], hello_fun, "A");
  nr_proc = 2;
  switch_boot_pcb();
}

Context* schedule(Context *prev) {
  if (current != NULL) {
    current->cp = prev;
  }

  if (current == &pcb_boot) {
    current = &pcb[0];
    slices_left = proc_slices[0];
  } else {
    int index = current - pcb;
    if (slices_left > 1) {
      slices_left --;
    } else {
      index = (index + 1) % nr_proc;
      current = &pcb[index];
      slices_left = proc_slices[index];
    }
  }
  return current->cp;
}
