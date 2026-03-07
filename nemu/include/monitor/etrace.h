#ifndef __MONITOR_ETRACE_H__
#define __MONITOR_ETRACE_H__

#include <common.h>

void etrace_log(word_t no, vaddr_t epc, vaddr_t target);

#endif
