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

#include <ultra64.h>
#include <ultra64/internal.h>
#include "pi.h"

typedef void io_func_t(u32 dev_addr, u32 ram_addr, size_t size);

static void pio_write(u32 dev_addr, u32 ram_addr, size_t size)
{
	int i;
	u32 dev_s;
	u32 dev_e;
	u32 dev_p;
	u32 ram_s;
	u32 ram_e;
	u32 ram_p;

	if (size == 0)
		return;

	dev_s = dev_addr & ~0x3;
	dev_e = (dev_addr + size + 0x3) & ~0x3;
	dev_p = dev_s;

	ram_s = ram_addr;
	ram_e = ram_s + size;
	ram_p = ram_addr - (dev_addr - dev_s);

	while (dev_p < dev_e)
	{
		u32 w = __pi_read_raw(dev_p);
		for (i = 0; i < 4; i++)
		{
			u8 b;
			if (ram_p >= ram_s && ram_p < ram_e)
				b = *(u8 *)ram_p;
			else
				b = w >> 24;
			w = (w << 8) | b;
			ram_p++;
		}
		__pi_write_raw(dev_p, w);
		dev_p += 4;
	}
}

static void pio_read(u32 dev_addr, u32 ram_addr, size_t size)
{
	int i;
	u32 dev_s;
	u32 dev_e;
	u32 dev_p;
	u32 ram_s;
	u32 ram_e;
	u32 ram_p;

	if (size == 0)
		return;

	dev_s = dev_addr & ~0x3;
	dev_e = (dev_addr + size + 0x3) & ~0x3;
	dev_p = dev_s;

	ram_s = ram_addr;
	ram_e = ram_s + size;
	ram_p = ram_addr - (dev_addr - dev_s);

	while (dev_p < dev_e)
	{
		u32 w = __pi_read_raw(dev_p);
		for (i = 0; i < 4; i++)
		{
			if (ram_p >= ram_s && ram_p < ram_e)
				*(u8 *)ram_p = w >> 24;
			w <<= 8;
			ram_p++;
		}
		dev_p += 4;
	}
}

static void dma_write(u32 dev_addr, u32 ram_addr, size_t size)
{
	OSMesgQueue mq;
	OSMesg m;
	__OSEventState pi_event;
	int irqf;

	if (size == 0)
		return;

	irqf = __osGetSR() & 1;
	if (irqf)
	{
		osCreateMesgQueue(&mq, &m, 1);

		pi_event = __osEventStateTab[OS_EVENT_PI];
		__osEventStateTab[OS_EVENT_PI].queue = &mq;
	}

	osWritebackDCache((void *) ram_addr, size);
	IO_WRITE(PI_DRAM_ADDR_REG, ram_addr & 0x1FFFFFFF);
	IO_WRITE(PI_CART_ADDR_REG, dev_addr & 0x1FFFFFFF);
	IO_WRITE(PI_RD_LEN_REG, size - 1);

	if (irqf)
	{
		osRecvMesg(&mq, NULL, OS_MESG_BLOCK);

		__osEventStateTab[OS_EVENT_PI] = pi_event;
	}
	else
	{
		__pi_wait();
		IO_WRITE(PI_STATUS_REG, PI_STATUS_CLR_INTR);
	}
}

static void dma_read(u32 dev_addr, u32 ram_addr, size_t size)
{
	OSMesgQueue mq;
	OSMesg m;
	__OSEventState pi_event;
	int irqf;

	if (size == 0)
		return;

	irqf = __osGetSR() & 1;
	if (irqf)
	{
		osCreateMesgQueue(&mq, &m, 1);

		pi_event = __osEventStateTab[OS_EVENT_PI];
		__osEventStateTab[OS_EVENT_PI].queue = &mq;
	}

	osWritebackDCache((void *) ram_addr, size);
	osInvalDCache((void *) ram_addr, size);
	IO_WRITE(PI_DRAM_ADDR_REG, ram_addr & 0x1FFFFFFF);
	IO_WRITE(PI_CART_ADDR_REG, dev_addr & 0x1FFFFFFF);
	IO_WRITE(PI_WR_LEN_REG, size - 1);

	if (irqf)
	{
		osRecvMesg(&mq, NULL, OS_MESG_BLOCK);

		__osEventStateTab[OS_EVENT_PI] = pi_event;
	}
	else
	{
		__pi_wait();
		IO_WRITE(PI_STATUS_REG, PI_STATUS_CLR_INTR);
	}
}

static void do_transfer(u32 dev_addr, u32 ram_addr, size_t size,
			io_func_t *pio_func, io_func_t *dma_func)
{
	if ((dev_addr ^ ram_addr) & 1)
	{
		/* Impossible alignment for DMA transfer,
		 * we have to PIO the whole thing.
		 */
		pio_func(dev_addr, ram_addr, size);
	}
	else
	{
		u32 ram_s = ram_addr;
		u32 ram_e = ram_addr + size;
		u32 ram_align_s = (ram_s + 0x7) & ~0x7;
		u32 dev_s = dev_addr;

		if (ram_e > ram_align_s)
		{
			u32 ram_align_e = ram_e & ~0x1;
			size_t pio_s = ram_align_s - ram_s;
			size_t pio_e = ram_e - ram_align_e;
			size_t dma = size - pio_s - pio_e;
			u32 dev_e = dev_addr + size;
			u32 dev_align_s = dev_s + pio_s;
			u32 dev_align_e = dev_e - pio_e;

			pio_func(dev_s, ram_s, pio_s);
			pio_func(dev_align_e, ram_align_e, pio_e);
			dma_func(dev_align_s, ram_align_s, dma);
		}
		else
		{
			pio_func(dev_s, ram_s, size);
		}
	}
}

void pi_write_locked(u32 dev_addr, const void *src, size_t size)
{
	do_transfer(dev_addr, (u32)src, size, pio_write, dma_write);
}

void pi_read_locked(u32 dev_addr, void *dst, size_t size)
{
	do_transfer(dev_addr, (u32)dst, size, pio_read, dma_read);
}

void pi_write(u32 dev_addr, const void *src, size_t size)
{
	__osPiGetAccess();
	pi_write_locked(dev_addr, src, size);
	__osPiRelAccess();
}

void pi_read(u32 dev_addr, void *dst, size_t size)
{
	__osPiGetAccess();
	pi_read_locked(dev_addr, dst, size);
	__osPiRelAccess();
}

int __pi_busy(void)
{
	return IO_READ(PI_STATUS_REG)
		& (PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY);
}

void __pi_wait(void)
{
	while (__pi_busy())
		;
}

u32 __pi_read_raw(u32 dev_addr)
{
	__pi_wait();
	return *(volatile u32 *)dev_addr;
}

void __pi_write_raw(u32 dev_addr, u32 value)
{
	__pi_wait();
	*(volatile u32 *)dev_addr = value;
}
