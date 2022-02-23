#ifndef PI_H
#define PI_H

#include <ultra64.h>

void pi_write_locked(u32 dev_addr, const void *src, size_t size);
void pi_read_locked(u32 dev_addr, void *dst, size_t size);
void pi_write(u32 dev_addr, const void *src, size_t size);
void pi_read(u32 dev_addr, void *dst, size_t size);

int __pi_busy(void);
void __pi_wait(void);
u32 __pi_read_raw(u32 dev_addr);
void __pi_write_raw(u32 dev_addr, u32 value);

#endif
