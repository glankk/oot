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
