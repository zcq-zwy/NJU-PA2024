#ifndef __SDB_SNAPSHOT_H__
#define __SDB_SNAPSHOT_H__

#include <common.h>

bool snapshot_save(const char *path);
bool snapshot_load(const char *path);

#endif
