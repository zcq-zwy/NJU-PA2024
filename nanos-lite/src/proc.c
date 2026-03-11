#include <proc.h>

#define MAX_NR_PROC 4
#define NTERM_SLICES 5
#define HELLO_SLICES 1

static PCB pcb[MAX_NR_PROC] __attribute__((used)) = {};
static PCB pcb_boot = {};
PCB *current = NULL;
static int nr_proc = 0;
static int slices_left = 0;

static int proc_slices[MAX_NR_PROC] = {
  [0] = NTERM_SLICES,
  [1] = HELLO_SLICES,
};

void switch_boot_pcb() {
  current = &pcb_boot;
}

void init_proc() {
  Log("Initializing processes...");

  static char *nterm_argv[] = { "/bin/nterm", NULL };
  static char *hello_argv[] = { "/bin/hello", NULL };
  int ret = context_uload(&pcb[0], "/bin/nterm", nterm_argv, NULL);
  assert(ret == 0);
  ret = context_uload(&pcb[1], "/bin/hello", hello_argv, NULL);
  assert(ret == 0);
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
