#ifndef IO_H
#define IO_H

#include "io_irqs.h"

int io_init(void);

unsigned long fifo_irqs(void);
int fifo_pwr(void);
int fifo_poll(void);
int fifo_read(void *dst, int n_bytes);
int fifo_write(const void *src, int n_bytes);

#endif
