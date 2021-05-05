#ifndef _ED64_V3_H_
#define _ED64_V3_H_

#include "ultra64.h"

int ed64_v3_fifo_write(const void *src, u32 n_blocks);
void *ed64_v3_proutSyncPrintf(void *arg, const char *str, u32 count);

#endif
