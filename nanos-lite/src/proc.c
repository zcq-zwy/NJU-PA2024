#include <proc.h>

#define MAX_NR_PROC 4
#define NR_FRONT_PROC 3
#define PAL_SLICES 5
#define BIRD_SLICES 5
#define NSLIDER_SLICES 5
#define HELLO_SLICES 1

static PCB pcb[MAX_NR_PROC] __attribute__((used)) = {};
static PCB pcb_boot = {};
PCB *current = NULL;
static int nr_proc = 0;
static int slices_left = 0;
static int fg_pcb = PCB_PAL;

static int proc_slices[MAX_NR_PROC] = {
  [PCB_PAL] = PAL_SLICES,
  [PCB_BIRD] = BIRD_SLICES,
  [PCB_NSLIDER] = NSLIDER_SLICES,
  [PCB_HELLO] = HELLO_SLICES,
};

static inline bool is_user_pcb(PCB *proc) {
  return proc >= pcb && proc < pcb + nr_proc;
}

void switch_boot_pcb() {
  current = &pcb_boot;
}

void switch_fg_pcb(int index) {
  assert(index >= 0 && index < NR_FRONT_PROC);
  if (fg_pcb != index) {
    fg_pcb = index;
    slices_left = proc_slices[fg_pcb];
    Log("Switch foreground to pcb[%d]", fg_pcb);
  }
}

void init_proc() {
  Log("Initializing processes...");

  static char *pal_argv[] = { "/bin/pal", NULL };
  static char *bird_argv[] = { "/bin/bird", NULL };
  static char *nslider_argv[] = { "/bin/nslider", NULL };
  static char *hello_argv[] = { "/bin/hello", NULL };
  int ret = context_uload(&pcb[PCB_PAL], "/bin/pal", pal_argv, NULL);
  assert(ret == 0);
  ret = context_uload(&pcb[PCB_BIRD], "/bin/bird", bird_argv, NULL);
  assert(ret == 0);
  ret = context_uload(&pcb[PCB_NSLIDER], "/bin/nslider", nslider_argv, NULL);
  assert(ret == 0);
  ret = context_uload(&pcb[PCB_HELLO], "/bin/hello", hello_argv, NULL);
  assert(ret == 0);
  nr_proc = MAX_NR_PROC;
  fg_pcb = PCB_PAL;
  switch_boot_pcb();
}

Context* schedule(Context *prev) {
  if (current != NULL && is_user_pcb(current)) {
    current->cp = prev;
  }

  if (current == &pcb_boot) {
    current = &pcb[fg_pcb];
    slices_left = proc_slices[fg_pcb];
  } else {
    int cur_index = current - pcb;
    if (cur_index == fg_pcb) {
      if (slices_left > 1) {
        slices_left --;
      } else {
        current = &pcb[PCB_HELLO];
        slices_left = proc_slices[PCB_HELLO];
      }
    } else if (cur_index == PCB_HELLO) {
      current = &pcb[fg_pcb];
      slices_left = proc_slices[fg_pcb];
    } else {
      current = &pcb[fg_pcb];
      slices_left = proc_slices[fg_pcb];
    }
  }
  return current->cp;
}
