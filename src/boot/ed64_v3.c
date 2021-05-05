#include <global.h>

#define REG_BASE          0xA8040000
#define REG_CFG           0
#define REG_STATUS        1
#define REG_DMA_LEN       2
#define REG_DMA_ADDR      3
#define REG_MSG           4
#define REG_DMA_CFG       5
#define REG_SPI           6
#define REG_SPI_CFG       7
#define REG_KEY           8
#define REG_SAV_CFG       9
#define REG_SEC           10
#define REG_VER           11
#define REG_CFG_CNT       16
#define REG_CFG_DAT       17
#define REG_MAX_MSG       18
#define REG_CRC           19
#define REGS_PTR          ((vu32 *)REG_BASE)

#define CFG_SDRAM_ON      0x0001
#define CFG_SWAP          0x0002
#define CFG_WR_MOD        0x0004
#define CFG_WR_ADDR_MASK  0x0008

#define STATUS_DMA_BUSY   0x0001
#define STATUS_DMA_TOUT   0x0002
#define STATUS_TXE        0x0004
#define STATUS_RXF        0x0008
#define STATUS_SPI        0x0010

#define DMA_SD_TO_RAM     0x0001
#define DMA_RAM_TO_SD     0x0002
#define DMA_FIFO_TO_RAM   0x0003
#define DMA_RAM_TO_FIFO   0x0004

#define SPI_SPEED_50      0x0000
#define SPI_SPEED_25      0x0001
#define SPI_SPEED_LO      0x0002
#define SPI_SPEED         0x0003
#define SPI_SS            0x0004
#define SPI_RD            0x0008
#define SPI_WR            0x0000
#define SPI_DAT           0x0010
#define SPI_CMD           0x0000
#define SPI_1CLK          0x0020
#define SPI_BYTE          0x0000

#define BLK_SIZE 512
#define CART_ADDR 0xB3FFF800

#define REG_RD(reg) \
	( \
		REGS_PTR[REG_CFG], \
		REGS_PTR[reg] \
	)

#define REG_WR(reg, dat) \
	( \
		REGS_PTR[REG_CFG], \
		REGS_PTR[reg] = dat \
	)

static u32 cart_irqf;
static u32 cart_lat;
static u32 cart_pwd;

static void cart_lock(void)
{
	__osPiGetAccess();
	cart_irqf = __osDisableInt();

	cart_lat = HW_REG(PI_BSD_DOM2_LAT_REG, u32);
	cart_pwd = HW_REG(PI_BSD_DOM2_PWD_REG, u32);

	HW_REG(PI_BSD_DOM2_LAT_REG, u32) = 4;
	HW_REG(PI_BSD_DOM2_PWD_REG, u32) = 12;

	REG_WR(REG_KEY, 0x1234);
}

static void cart_unlock(void)
{
	REG_WR(REG_KEY, 0x0000);

	HW_REG(PI_BSD_DOM2_LAT_REG, u32) = cart_lat;
	HW_REG(PI_BSD_DOM2_PWD_REG, u32) = cart_pwd;

	__osPiRelAccess();
	__osRestoreInt(cart_irqf);
}

int ed64_v3_fifo_write(const void *src, u32 n_blocks)
{
	cart_lock();

	{
		/* copy to cart */
		u32 n_bytes = BLK_SIZE * n_blocks;

		osWritebackDCache((void *)src, n_bytes);
		HW_REG(PI_DRAM_ADDR_REG, u32) = (u32)src & 0x1FFFFFFF;
		HW_REG(PI_CART_ADDR_REG, u32) = CART_ADDR & 0x1FFFFFFF;
		HW_REG(PI_RD_LEN_REG, u32) = n_bytes - 1;
		while (HW_REG(PI_STATUS_REG, u32) & PI_STATUS_BUSY)
			;
		HW_REG(PI_STATUS_REG, u32) = PI_STATUS_CLEAR_INTR;
	}

	/* wait for tx buffer empty (TXE low) */
	while (REG_RD(REG_STATUS) & STATUS_TXE)
		;

	/* dma cart to fifo */
	REG_WR(REG_DMA_LEN, n_blocks - 1);
	REG_WR(REG_DMA_ADDR, CART_ADDR >> 11);
	REG_WR(REG_DMA_CFG, DMA_RAM_TO_FIFO);
	while (REG_RD(REG_STATUS) & STATUS_DMA_BUSY)
		;

	/* check for dma timeout */
	if (REG_RD(REG_STATUS) & STATUS_DMA_TOUT) {
		cart_unlock();
		return -1;
	}

	cart_unlock();

	return 0;
}

static union
{
	char		data[BLK_SIZE];
	long long int	force_structure_alignment;
} blk;
static u32 blk_p;

void *ed64_v3_proutSyncPrintf(void *arg, const char *str, u32 count)
{
	while (count != 0)
	{
		u32 chunk;
		u32 rem = BLK_SIZE - blk_p;
		if (count < rem)
			chunk = count;
		else
			chunk = rem;

		memcpy(&blk.data[blk_p], str, chunk);
		str += chunk;
		count -= chunk;
		blk_p += chunk;

		if (blk_p == BLK_SIZE)
		{
			ed64_v3_fifo_write(blk.data, 1);
			blk_p = 0;
		}
	}

	return (void *)1;
}
