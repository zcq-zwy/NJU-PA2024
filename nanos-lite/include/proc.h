#ifndef __PROC_H__
#define __PROC_H__

#include <common.h>
#include <memory.h>

#define STACK_SIZE (8 * PGSIZE)

enum {
  PCB_PAL = 0,
  PCB_BIRD,
  PCB_ONSCRIPTER,
  PCB_SYSMON,
};

typedef union {
  uint8_t stack[STACK_SIZE] PG_ALIGN;
  struct {
    Context *cp;
    AddrSpace as;
    uintptr_t max_brk;
  };
} PCB;

extern PCB *current;

void naive_uload(PCB *pcb, const char *filename);
int context_uload(PCB *pcb, const char *filename, char *const argv[], char *const envp[]);
void switch_fg_pcb(int index);
void switch_boot_pcb(void);
void init_proc(void);
Context *schedule(Context *prev);
void proc_sched_stats(uint64_t *fg_runs, uint64_t *bg_runs, uint64_t *total_runs);

#endif
