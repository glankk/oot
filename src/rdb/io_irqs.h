#ifndef IO_IRQS_H
#define IO_IRQS_H

#include <ultra64.h>

#define FIFO_IRQ_OFF	(1 << 0)
#define FIFO_IRQ_RX	(1 << 1)

OSMesg fifo_spin(OSMesgQueue *mq, int irqs);
OSMesg fifo_sleep(OSMesgQueue *mq, int irqs);

#endif
