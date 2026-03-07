#ifndef __MONITOR_FTRACE_H__
#define __MONITOR_FTRACE_H__

#include <common.h>

void init_ftrace(int elf_count, char *elf_files[]);
void ftrace_call(vaddr_t pc, vaddr_t target);
void ftrace_ret(vaddr_t pc, vaddr_t target);

#endif
