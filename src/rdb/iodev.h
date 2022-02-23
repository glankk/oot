#ifndef IODEV_H
#define IODEV_H

#include <ultra64.h>

struct iodev
{
	int		(*probe)	(void);

	unsigned long	(*fifo_irqs)	(void);
	int		(*fifo_pwr)	(void);
	int		(*fifo_poll)	(void);
	int		(*fifo_read)	(void *dst, int n_bytes);
	int		(*fifo_write)	(const void *src, int n_bytes);
};

#endif
