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
#include "hb.h"
#include "io_irqs.h"
#include "iodev.h"

static int fifo_can_rd(void)
{
	u32 sta = hb_regs.fifo_status;

	return (sta & (HB_FIFO_STA_PWR | HB_FIFO_STA_RXF)) == HB_FIFO_STA_PWR;
}

static int fifo_can_wr(void)
{
	u32 sta = hb_regs.fifo_status;

	return (sta & (HB_FIFO_STA_PWR | HB_FIFO_STA_TXE)) == HB_FIFO_STA_PWR;
}

static int probe(void)
{
	if (hb_regs.key == 0x1234)
		return 0;
	else
		return -1;
}

static unsigned long fifo_irqs(void)
{
	unsigned long ret = 0;

	u32 sta = hb_regs.fifo_status;

	if ((sta & HB_FIFO_STA_PWR) == 0)
		ret |= FIFO_IRQ_OFF;

	if ((sta & (HB_FIFO_STA_PWR | HB_FIFO_STA_RXF)) == HB_FIFO_STA_PWR)
		ret |= FIFO_IRQ_RX;

	return ret;
}

static int fifo_pwr(void)
{
	u32 sta = hb_regs.fifo_status;

	return (sta & HB_FIFO_STA_PWR) == HB_FIFO_STA_PWR;
}

static int fifo_poll(void)
{
	return fifo_can_rd();
}

static int fifo_read(void *ptr, int n_bytes)
{
	if (n_bytes == 0)
		return 0;

	while (!fifo_can_rd())
		;

	hb_regs.fifo_dram_addr = K0_TO_PHYS(ptr);
	hb_regs.fifo_rd_len = n_bytes;

	return n_bytes - hb_regs.fifo_rd_len;
}

static int fifo_write(const void *ptr, int n_bytes)
{
	if (n_bytes == 0)
		return 0;

	while (!fifo_can_wr())
		;

	hb_regs.fifo_dram_addr = K0_TO_PHYS(ptr);
	hb_regs.fifo_wr_len = n_bytes;

	return n_bytes - hb_regs.fifo_wr_len;
}

struct iodev homeboy_iodev =
{
	probe,

	fifo_irqs,
	fifo_pwr,
	fifo_poll,
	fifo_read,
	fifo_write,
};
