#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <common.h>

#ifndef PGSIZE
#define PGSIZE 4096
#endif

#define PG_ALIGN __attribute((aligned(PGSIZE)))

void* new_page(size_t);
int mm_brk(uintptr_t brk);
size_t mm_used_bytes(void);
size_t mm_total_bytes(void);

#endif
