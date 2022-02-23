/*
 * Copyright (c) 2022, 2023 glank
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <array_count.h>
#include <ultra64.h>
#include <ultra64/internal.h>
#include "ed64_io.h"
#include "hb_io.h"
#include "io_irqs.h"
#include "iodev.h"

static struct iodev *current_dev;

int io_init(void)
{
	int i;

	struct iodev *devs[] =
	{
		&homeboy_iodev,
		&everdrive64_x,
	};

	for (i = 0; i < ARRAY_COUNT(devs); i++)
	{
		current_dev = devs[i];
		if (current_dev->probe() == 0)
			return 0;
	}

	current_dev = NULL;
	return -1;
}

unsigned long fifo_irqs(void)
{
	if (current_dev && current_dev->fifo_irqs)
		return current_dev->fifo_irqs();
	else
		return 0;
}

int fifo_pwr(void)
{
	if (current_dev && current_dev->fifo_pwr)
		return current_dev->fifo_pwr();
	else
		return 0;
}

int fifo_poll(void)
{
	if (current_dev && current_dev->fifo_poll)
		return current_dev->fifo_poll();
	else
		return 0;
}

int fifo_read(void *dst, int n_bytes)
{
	if (current_dev && current_dev->fifo_read)
		return current_dev->fifo_read(dst, n_bytes);
	else
		return -1;
}

int fifo_write(const void *src, int n_bytes)
{
	if (current_dev && current_dev->fifo_write)
		return current_dev->fifo_write(src, n_bytes);
	else
		return -1;
}

OSMesg fifo_spin(OSMesgQueue *mq, int irqs)
{
	for (;;)
	{
		OSMesg mesg;

		if ((irqs & FIFO_IRQ_OFF) && !fifo_pwr())
		{
			return (OSMesg) OS_EVENT_FIFO_OFF;
		}
		else if ((irqs & FIFO_IRQ_RX) && fifo_poll())
		{
			return (OSMesg) OS_EVENT_FIFO_RX;
		}
		else if (mq != NULL
			&& osRecvMesg(mq, &mesg, OS_MESG_NOBLOCK) == 0)
		{
			return mesg;
		}
	}
}

OSMesg fifo_sleep(OSMesgQueue *mq, int irqs)
{
	if ((irqs & FIFO_IRQ_OFF) && !fifo_pwr())
	{
		return (OSMesg) OS_EVENT_FIFO_OFF;
	}
	else if ((irqs & FIFO_IRQ_RX) && fifo_poll())
	{
		return (OSMesg) OS_EVENT_FIFO_RX;
	}
	else
	{
		OSMesg mesg;

		__OSEventState *off_event;
		__OSEventState *rx_event;

		int irqf = __osDisableInt();

		off_event = &__osEventStateTab[OS_EVENT_FIFO_OFF];
		rx_event = &__osEventStateTab[OS_EVENT_FIFO_RX];

		if (irqs & FIFO_IRQ_OFF)
		{
			off_event->queue = mq;
			off_event->msg = (OSMesg) OS_EVENT_FIFO_OFF;
		}
		if (irqs & FIFO_IRQ_RX)
		{
			rx_event->queue = mq;
			rx_event->msg = (OSMesg) OS_EVENT_FIFO_RX;
		}

		osRecvMesg(mq, &mesg, OS_MESG_BLOCK);

		if (irqs & FIFO_IRQ_OFF)
		{
			off_event->queue = NULL;
		}
		if (irqs & FIFO_IRQ_RX)
		{
			rx_event->queue = NULL;
		}

		__osRestoreInt(irqf);

		return mesg;
	}
}

unsigned long __fifo_irqs_masked(void)
{
	int mask = 0;

	OSMesgQueue *off_queue = __osEventStateTab[OS_EVENT_FIFO_OFF].queue;
	OSMesgQueue *rx_queue = __osEventStateTab[OS_EVENT_FIFO_RX].queue;

	if (off_queue != NULL && off_queue->mtqueue->priority != -1)
	{
		mask |= FIFO_IRQ_OFF;
	}
	if (rx_queue != NULL && rx_queue->mtqueue->priority != -1)
	{
		mask |= FIFO_IRQ_RX;
	}

	return fifo_irqs() & mask;
}
