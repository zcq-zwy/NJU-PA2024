#include <proc.h>

#define MAX_NR_PROC 4
#define NR_FRONT_PROC 3
#define PAL_SLICES 5
#define BIRD_SLICES 5
#define ONSCRIPTER_SLICES 5
#define SYSMON_SLICES 1
#define SYSMON_PERIOD 4

static PCB pcb[MAX_NR_PROC] __attribute__((used)) = {};
static PCB pcb_boot = {};
PCB *current = NULL;
static int nr_proc = 0;
static int slices_left = 0;
static int fg_pcb = PCB_PAL;
static int sysmon_quota = SYSMON_PERIOD;
static uint64_t fg_runs = 0;
static uint64_t bg_runs = 0;

static int proc_slices[MAX_NR_PROC] = {
  [PCB_PAL] = PAL_SLICES,
  [PCB_BIRD] = BIRD_SLICES,
  [PCB_ONSCRIPTER] = ONSCRIPTER_SLICES,
  [PCB_SYSMON] = SYSMON_SLICES,
};

static inline bool is_user_pcb(PCB *proc) {
  return proc >= pcb && proc < pcb + nr_proc;
}

void reset_audio_on_switch(void);

void switch_boot_pcb() {
  current = &pcb_boot;
}

void switch_fg_pcb(int index) {
  assert(index >= 0 && index < NR_FRONT_PROC);
  if (fg_pcb != index) {
    reset_audio_on_switch();
    fg_pcb = index;
    slices_left = proc_slices[fg_pcb];
    Log("Switch foreground to pcb[%d]", fg_pcb);
  }
}

void init_proc() {
  Log("Initializing processes...");

  static char *pal_argv[] = { "/bin/pal", NULL };
  static char *bird_argv[] = { "/bin/bird", NULL };
  static char *onscripter_argv[] = {
    "/bin/onscripter", "-r", "/share/games/planetarian", NULL
  };
  static char *sysmon_argv[] = { "/bin/sysmon", NULL };
  int ret = context_uload(&pcb[PCB_PAL], "/bin/pal", pal_argv, NULL);
  assert(ret == 0);
  ret = context_uload(&pcb[PCB_BIRD], "/bin/bird", bird_argv, NULL);
  assert(ret == 0);
  ret = context_uload(&pcb[PCB_ONSCRIPTER], "/bin/onscripter", onscripter_argv, NULL);
  assert(ret == 0);
  ret = context_uload(&pcb[PCB_SYSMON], "/bin/sysmon", sysmon_argv, NULL);
  assert(ret == 0);
  nr_proc = MAX_NR_PROC;
  fg_pcb = PCB_PAL;
  sysmon_quota = SYSMON_PERIOD;
  fg_runs = 0;
  bg_runs = 0;
  switch_boot_pcb();
}

Context* schedule(Context *prev) {
  if (current != NULL && is_user_pcb(current)) {
    current->cp = prev;
  }

  if (current == &pcb_boot) {
    current = &pcb[fg_pcb];
    slices_left = proc_slices[fg_pcb];
    fg_runs ++;
  } else {
    int cur_index = current - pcb;
    if (cur_index == fg_pcb) {
      if (slices_left > 1) {
        slices_left --;
      } else {
        if (sysmon_quota > 1) {
          sysmon_quota --;
          current = &pcb[fg_pcb];
          slices_left = proc_slices[fg_pcb];
          fg_runs ++;
        } else {
          sysmon_quota = SYSMON_PERIOD;
          current = &pcb[PCB_SYSMON];
          slices_left = proc_slices[PCB_SYSMON];
          bg_runs ++;
        }
      }
    } else if (cur_index == PCB_SYSMON) {
      current = &pcb[fg_pcb];
      slices_left = proc_slices[fg_pcb];
      fg_runs ++;
    } else {
      current = &pcb[fg_pcb];
      slices_left = proc_slices[fg_pcb];
      fg_runs ++;
    }
  }
  return current->cp;
}

void proc_sched_stats(uint64_t *fg, uint64_t *bg, uint64_t *total) {
  if (fg != NULL) *fg = fg_runs;
  if (bg != NULL) *bg = bg_runs;
  if (total != NULL) *total = fg_runs + bg_runs;
}
