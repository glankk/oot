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
#include "ed64_x.h"
#include "io_irqs.h"
#include "iodev.h"
#include "pi.h"

#define USB_CMD_RD		(USB_LE_CFG | USB_CFG_RD | USB_CFG_ACT)
#define USB_CMD_RD_NOP		(USB_LE_CFG | USB_CFG_RD)
#define USB_CMD_WR		(USB_LE_CFG | USB_CFG_WR | USB_CFG_ACT)
#define USB_CMD_WR_NOP		(USB_LE_CFG | USB_CFG_WR)
#define USB_CMD_CTR(ctr)	(USB_LE_CTR | ((ctr) & USB_CFG_CTR))

static int cart_irqf;
static u32 cart_lat;
static u32 cart_pwd;

static void cart_lock_safe(void)
{
	__osPiGetAccess();

	cart_irqf = __osDisableInt();

	cart_lat = IO_READ(PI_BSD_DOM1_LAT_REG);
	cart_pwd = IO_READ(PI_BSD_DOM1_PWD_REG);
}

static void cart_lock(void)
{
	cart_lock_safe();

	IO_WRITE(PI_BSD_DOM1_LAT_REG, 4);
	IO_WRITE(PI_BSD_DOM1_PWD_REG, 12);
}

static void cart_unlock(void)
{
	IO_WRITE(PI_BSD_DOM1_LAT_REG, cart_lat);
	IO_WRITE(PI_BSD_DOM1_PWD_REG, cart_pwd);

	__osPiRelAccess();

	__osRestoreInt(cart_irqf);
}

#define reg_rd(reg) __pi_read_raw((u32) &REGS_PTR[reg])

#define reg_wr(reg, dat) __pi_write_raw((u32) &REGS_PTR[reg], dat)

static int fifo_can_rd(void)
{
	u32 cfg = reg_rd(REG_USB_CFG);

	return (cfg & (USB_STA_PWR | USB_STA_RXF)) == USB_STA_PWR;
}

static int fifo_can_wr(void)
{
	u32 cfg = reg_rd(REG_USB_CFG);

	return (cfg & (USB_STA_PWR | USB_STA_TXE)) == USB_STA_PWR;
}

static int probe(void)
{
	cart_lock_safe();

	/* open registers */
	reg_wr(REG_KEY, 0xAA55);

	/* check magic number */
	if ((reg_rd(REG_EDID) >> 16) != 0xED64)
		goto nodev;

	cart_unlock();
	return 0;

nodev:
	reg_wr(REG_KEY, 0);
	cart_unlock();
	return -1;
}

static unsigned long fifo_irqs(void)
{
	unsigned long ret = 0;

	if (!__pi_busy())
	{
		u32 cfg = reg_rd(REG_USB_CFG);

		if ((cfg & USB_STA_PWR) == 0)
			ret |= FIFO_IRQ_OFF;

		if ((cfg & (USB_STA_PWR | USB_STA_RXF)) == USB_STA_PWR)
			ret |= FIFO_IRQ_RX;
	}

	return ret;
}

static int fifo_pwr(void)
{
	int ret;

	cart_lock();
	{
		u32 cfg = reg_rd(REG_USB_CFG);

		if ((cfg & USB_STA_PWR) == USB_STA_PWR)
			ret = 1;
		else
			ret = 0;
	}
	cart_unlock();

	return ret;
}

static int fifo_poll(void)
{
	int ret;

	cart_lock();
	ret = fifo_can_rd();
	cart_unlock();

	return ret;
}

static int fifo_read(void *ptr, int n_bytes)
{
	int n_rd;
	char *p;
	int chunk_size;
	int tout;
	int ctr;
	int cfg;
	int new_ctr;
	char *usb_dat;

	if (n_bytes == 0)
		return 0;

	cart_lock();

	/* wait for some data */
	while (!fifo_can_rd())
		;

	n_rd = 0;
	p = ptr;
	do
	{
		/* receive */
		chunk_size = 512;
		if (chunk_size > n_bytes)
			chunk_size = n_bytes;

		tout = 0;
		ctr = 512 - chunk_size;
		reg_wr(REG_USB_CFG, USB_CMD_RD | USB_CMD_CTR(ctr));
		for (;;)
		{
			cfg = reg_rd(REG_USB_CFG);
			if ((cfg & USB_STA_ACT) == 0)
				break;
			else if ((cfg & USB_STA_RXF) == USB_STA_RXF)
				tout++;
			else
				tout = 0;
			if (tout == 8196)
				break;
		}
		reg_wr(REG_USB_CFG, USB_CMD_RD_NOP);

		cfg = reg_rd(REG_USB_CFG);
		new_ctr = cfg & USB_STA_CTR;
		if (new_ctr == 0)
			new_ctr = 512;
		chunk_size = new_ctr - ctr;

		/* copy from rx buffer */
		usb_dat = (void *) &REGS_PTR[REG_USB_DAT];
		/* Quirk: When the ending ctr is odd, the last 16-bit word is
		 * swapped. The last byte is at new_ctr instead of new_ctr - 1.
		 */
		if (new_ctr & 1)
		{
			if (chunk_size > 1)
			{
				pi_read_locked((u32) &usb_dat[ctr], p,
						chunk_size - 1);
			}
			pi_read_locked((u32) &usb_dat[new_ctr],
					&p[chunk_size - 1], 1);
		}
		else
		{
			pi_read_locked((u32) &usb_dat[ctr], p, chunk_size);
		}

		n_bytes -= chunk_size;
		n_rd += chunk_size;
		p += chunk_size;
	}
	while (n_bytes != 0 && fifo_can_rd());

	cart_unlock();
	return n_rd;
}

static int fifo_write(const void *ptr, int n_bytes)
{
	int n_wr;
	const char *p;
	int chunk_size;
	int ctr;
	char *usb_dat;

	cart_lock();

	n_wr = 0;
	p = ptr;
	while (n_bytes != 0)
	{
		chunk_size = 512;
		if (chunk_size > n_bytes)
			chunk_size = n_bytes;

		/* wait for power on and tx buffer empty (PWR high, TXE low) */
		while (!fifo_can_wr())
			;

		/* copy to tx buffer */
		ctr = 512 - chunk_size;
		reg_wr(REG_USB_CFG, USB_CMD_WR_NOP | USB_CMD_CTR(ctr));
		usb_dat = (void *) &REGS_PTR[REG_USB_DAT];
		pi_write_locked((u32) &usb_dat[ctr], p, chunk_size);

		/* transmit */
		reg_wr(REG_USB_CFG, USB_CMD_WR | USB_CMD_CTR(ctr));
		while (reg_rd(REG_USB_CFG) & USB_STA_ACT)
			;

		n_bytes -= chunk_size;
		n_wr += chunk_size;
		p += chunk_size;
	}

	cart_unlock();
	return n_wr;
}

struct iodev everdrive64_x =
{
	probe,

	fifo_irqs,
	fifo_pwr,
	fifo_poll,
	fifo_read,
	fifo_write,
};
