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
#include <effect.h>
#include <kaleido_manager.h>
#include <ultra64.h>
#include <z_actor_dlftbls.h>
#include <z_game_dlftbls.h>
#include "io.h"
#include "rdb.h"
#include "rdb_private.h"
#include "vr4300.h"

static struct rdb	rdb;
static OSThread		rdb_thread;
static u64		rdb_stack[0x200];
static OSMesgQueue	rdb_mq;
static OSMesg		rdb_mesg[8];
static struct rdb_lib *	rdb_changed_lib;

static int hex_char(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static char hex_int(int d)
{
	if (d < 10)
		return d + '0';
	else
		return d + 'a' - 10;
}

static int check_addr(uintptr_t addr, size_t size)
{
	return addr >= 0x80000000
		&& addr < 0xC0000000
		&& (addr & (size - 1)) == 0;
}

static int rx_poll(void)
{
	return rdb.rx_pos != rdb.rx_size || fifo_poll();
}

static void rx_flush(void)
{
	size_t size = rdb.rx_size - rdb.rx_pos;

	bcopy(&rdb.rx_buf[rdb.rx_pos], &rdb.rx_buf[0], size);
	rdb.rx_size = size;
	rdb.rx_pos = 0;
}

static void rx_discard(size_t n)
{
	size_t size;
	size_t pos;
	size_t move_size;

	if (n > rdb.rx_size)
		n = rdb.rx_size;

	size = rdb.rx_size - n;
	pos = rdb.rx_pos - n;
	move_size = rdb.rx_size - rdb.rx_pos;

	bcopy(&rdb.rx_buf[rdb.rx_pos], &rdb.rx_buf[pos], move_size);
	rdb.rx_size = size;
	rdb.rx_pos = pos;
}

static void rx_putc(char c)
{
	size_t size;
	size_t pos;
	size_t move_size;

	if (rdb.rx_pos == rdb.rx_size)
		return;

	size = rdb.rx_size + 1;
	pos = rdb.rx_pos + 1;
	move_size = rdb.rx_size - pos;

	bcopy(&rdb.rx_buf[rdb.rx_pos], &rdb.rx_buf[pos], move_size);
	rdb.rx_buf[rdb.rx_pos] = c;
	rdb.rx_size = size;
	rdb.rx_pos = pos;
}

static char rx_getc(void)
{
	if (rdb.rx_pos == rdb.rx_size)
	{
		rdb.rx_size = fifo_read(&rdb.rx_buf, sizeof(rdb.rx_buf));
		rdb.rx_pos = 0;
	}
	return rdb.rx_buf[rdb.rx_pos++];
}

static void tx_flush(void)
{
	if (rdb.tx_size != 0)
	{
		fifo_write(&rdb.tx_buf, rdb.tx_size);
		rdb.tx_size = 0;
	}
}

static void tx_putc(char c)
{
	if (rdb.tx_size == sizeof(rdb.tx_buf))
		tx_flush();

	rdb.tx_buf[rdb.tx_size++] = c;
}

static void pkt_putc_raw(char c)
{
	tx_putc(c);

	if (rdb.opkt_type != PKT_NONE)
		rdb.opkt_csum += c;
}

static void pkt_putc_esc(char c)
{
	if (c == '#' || c == '$' || c == '%' || c == '*' || c == '}')
	{
		pkt_putc_raw('}');
		pkt_putc_raw(c ^ ' ');
	}
	else
	{
		pkt_putc_raw(c);
	}
}

static void pkt_do_one_rle(void)
{
	if (rdb.opkt_rle_len < 4)
	{
		/* Too short for RLE, put plain-text */
		while (rdb.opkt_rle_len != 0)
		{
			pkt_putc_esc(rdb.opkt_rle_chr);
			rdb.opkt_rle_len--;
		}
	}
	else
	{
		int len = rdb.opkt_rle_len;
		if (len > 98)
		{
			/* Too long, truncate to max length */
			len = 98;
		}
		else if (len == 7 || len == 8)
		{
			/* Bad length, truncate to nearest valid */
			len = 6;
		}

		pkt_putc_esc(rdb.opkt_rle_chr);
		pkt_putc_raw('*');
		pkt_putc_raw(len - 4 + ' ');

		rdb.opkt_rle_len -= len;
	}
}

static void pkt_flush_rle(void)
{
	/* RLE's may need to be broken into multiple sequences for various
	 * reasons
	 */
	while (rdb.opkt_rle_len != 0)
		pkt_do_one_rle();
}

static void pkt_flush(void)
{
	pkt_flush_rle();
}

static void pkt_putc(char c)
{
	if (c == rdb.opkt_rle_chr)
	{
		rdb.opkt_rle_len++;
	}
	else
	{
		pkt_flush_rle();
		rdb.opkt_rle_chr = c;
		rdb.opkt_rle_len = 1;
	}
}

static void pkt_puts(const char *s)
{
	while (s[0] != '\0')
	{
		pkt_putc(s[0]);
		s++;
	}
}

static void pkt_putx(u64 v)
{
	int i;

	/* Skip leading zeros, but keep at least one digit */
	for (i = 0; i < 15 && (v >> 60) == 0; i++)
		v = v << 4;

	for (; i < 16; i++)
	{
		pkt_putc(hex_int(v >> 60));
		v = v << 4;
	}
}

static void pkt_putx_n(u64 v, int n)
{
	int i;

	v = v << ((16 - n) << 2);

	for (i = 0; i < n; i++)
	{
		pkt_putc(hex_int(v >> 60));
		v = v << 4;
	}
}

static void pkt_start(int notif)
{
	/* For simplicity we reset the transmit buffer here. When it comes time
	 * to actually transmit the packet we can assume that the tx buffer
	 * contains exactly the entire packet and nothing else, which is
	 * convenient if we need to retransmit. This assumes that the packet
	 * size never exceeds the size of the tx buffer.
	 */
	tx_flush();

	if (notif)
	{
		tx_putc('%');
		rdb.opkt_type = PKT_NOTIF;
	}
	else
	{
		tx_putc('$');
		rdb.opkt_type = PKT_NORM;
	}
	rdb.opkt_csum = 0;
}

static int pkt_finish(void)
{
	enum pkt_type pkt_type;
	size_t pkt_size;

	pkt_flush();

	tx_putc('#');
	tx_putc(hex_int(rdb.opkt_csum >> 4));
	tx_putc(hex_int(rdb.opkt_csum & 0xF));

	pkt_type = rdb.opkt_type;
	pkt_size = rdb.tx_size;
	rdb.opkt_type = PKT_NONE;

	for (;;)
	{
		rdb.tx_size = pkt_size;
		tx_flush();

		if (pkt_type == PKT_NORM && !rdb.noack)
		{
			char ack = rx_getc();
			if (ack == '+')
				return 0;
			else if (ack != '-')
				return -1;
		}
		else
		{
			return 0;
		}
	}
}

static int putpkt(int notif, const char *pkt)
{
	pkt_start(notif);
	pkt_puts(pkt);
	return pkt_finish();
}

static int getpkt(int notif)
{
	int rx_csum = 0;
	u8 csum = 0;

	rx_flush();

	rdb.ipkt_size = 0;
	rdb.ipkt_pos = 0;

	/* Receive packet data */
	for (;;)
	{
		char c = rx_getc();

		/* Check for packet terminator */
		if (c == '#')
			break;

		csum += c;

		/* Check for escape sequence */
		if (c == '}')
		{
			c = rx_getc();
			csum += c;
			c ^= ' ';

			rx_discard(2);
			rx_putc(c);
		}

		rdb.ipkt_size++;
	}

	/* Receive checksum */
	rx_csum = (rx_csum << 4) | hex_char(rx_getc());
	rx_csum = (rx_csum << 4) | hex_char(rx_getc());

	if (rx_csum == csum)
	{
		/* Checksum ok; acknowledge */
		if (!rdb.noack && !notif)
		{
			tx_putc('+');
			tx_flush();
		}

		return 0;
	}
	else
	{
		/* Checksum failed; drop packet if it's a notification.
		 * Otherwise, request a retransmission.
		 */
		if (!rdb.noack && !notif)
		{
			tx_putc('-');
			tx_flush();
		}

		return -1;
	}
}

static int pkt_scan_eof(void)
{
	if (rdb.ipkt_pos == rdb.ipkt_size)
		return 0;
	else
		return -1;
}

static int pkt_scan_lit(const char *s)
{
	size_t p = rdb.ipkt_pos;

	while (s[0] != '\0')
	{
		if (p == rdb.ipkt_size)
			return s[0] - '\0';

		if (s[0] != rdb.rx_buf[p])
			return s[0] - rdb.rx_buf[p];

		p++;
		s++;
	}

	rdb.ipkt_pos = p;

	return 0;
}

static int pkt_scan_c(char *c)
{
	if (rdb.ipkt_pos == rdb.ipkt_size)
	{
		return -1;
	}
	else
	{
		*c = rdb.rx_buf[rdb.ipkt_pos];
		rdb.ipkt_pos++;

		return 0;
	}
}

static int pkt_scan_x8(u8 *vp, int l)
{
	u8 v = 0;
	int n = 0;

	if (l < 1)
		l = -1;

	while (rdb.ipkt_pos + n != rdb.ipkt_size && n != l)
	{
		char c = rdb.rx_buf[rdb.ipkt_pos + n];

		int x = hex_char(c);
		if (x == -1)
		{
			break;
		}
		else
		{
			v = (v << 4) | x;
			n++;
		}
	}

	if (l == -1)
		l = n;

	if (n != 0 && n == l)
	{
		*vp = v;
		rdb.ipkt_pos += n;

		return 0;
	}
	else
	{
		return -1;
	}
}

static int pkt_scan_x32(u32 *vp, int l)
{
	u32 v = 0;
	int n = 0;

	if (l < 1)
		l = -1;

	while (rdb.ipkt_pos + n != rdb.ipkt_size && n != l)
	{
		char c = rdb.rx_buf[rdb.ipkt_pos + n];

		int x = hex_char(c);
		if (x == -1)
		{
			break;
		}
		else
		{
			v = (v << 4) | x;
			n++;
		}
	}

	if (l == -1)
		l = n;

	if (n != 0 && n == l)
	{
		*vp = v;
		rdb.ipkt_pos += n;

		return 0;
	}
	else
	{
		return -1;
	}
}

static int pkt_scan_x64(u64 *vp, int l)
{
	u64 v = 0;
	int n = 0;

	if (l < 1)
		l = -1;

	while (rdb.ipkt_pos + n != rdb.ipkt_size && n != l)
	{
		char c = rdb.rx_buf[rdb.ipkt_pos + n];

		int x = hex_char(c);
		if (x == -1)
		{
			break;
		}
		else
		{
			v = (v << 4) | x;
			n++;
		}
	}

	if (l == -1)
		l = n;

	if (n != 0 && n == l)
	{
		*vp = v;
		rdb.ipkt_pos += n;

		return 0;
	}
	else
	{
		return -1;
	}
}

static u64 get_reg(OSThread *thread, int reg_idx)
{
	__OSThreadContext *c = &thread->context;
	u64 *f = (void *) &c->fp0;
	switch (reg_idx)
	{
	case 0x01:	return c->at;
	case 0x02:	return c->v0;
	case 0x03:	return c->v1;
	case 0x04:	return c->a0;
	case 0x05:	return c->a1;
	case 0x06:	return c->a2;
	case 0x07:	return c->a3;
	case 0x08:	return c->t0;
	case 0x09:	return c->t1;
	case 0x0A:	return c->t2;
	case 0x0B:	return c->t3;
	case 0x0C:	return c->t4;
	case 0x0D:	return c->t5;
	case 0x0E:	return c->t6;
	case 0x0F:	return c->t7;
	case 0x10:	return c->s0;
	case 0x11:	return c->s1;
	case 0x12:	return c->s2;
	case 0x13:	return c->s3;
	case 0x14:	return c->s4;
	case 0x15:	return c->s5;
	case 0x16:	return c->s6;
	case 0x17:	return c->s7;
	case 0x18:	return c->t8;
	case 0x19:	return c->t9;
	case 0x1C:	return c->gp;
	case 0x1D:	return c->sp;
	case 0x1E:	return c->s8;
	case 0x1F:	return c->ra;
	case 0x20:	return c->sr;
	case 0x21:	return c->lo;
	case 0x22:	return c->hi;
	case 0x23:	return c->badvaddr;
	case 0x24:	return c->cause;
	case 0x25:	return c->pc;
	case 0x26:	return f[0];
	case 0x28:	return f[1];
	case 0x2A:	return f[2];
	case 0x2C:	return f[3];
	case 0x2E:	return f[4];
	case 0x30:	return f[5];
	case 0x32:	return f[6];
	case 0x34:	return f[7];
	case 0x36:	return f[8];
	case 0x38:	return f[9];
	case 0x3A:	return f[10];
	case 0x3C:	return f[11];
	case 0x3E:	return f[12];
	case 0x40:	return f[13];
	case 0x42:	return f[14];
	case 0x44:	return f[15];
	case 0x46:	return c->fpcsr;
	}
	return 0;
}

static void set_reg(OSThread *thread, int reg_idx, u64 value)
{
	__OSThreadContext *c = &thread->context;
	u64 *f = (void *) &c->fp0;
	switch (reg_idx)
	{
	case 0x01:	c->at		= value;	break;
	case 0x02:	c->v0		= value;	break;
	case 0x03:	c->v1		= value;	break;
	case 0x04:	c->a0		= value;	break;
	case 0x05:	c->a1		= value;	break;
	case 0x06:	c->a2		= value;	break;
	case 0x07:	c->a3		= value;	break;
	case 0x08:	c->t0		= value;	break;
	case 0x09:	c->t1		= value;	break;
	case 0x0A:	c->t2		= value;	break;
	case 0x0B:	c->t3		= value;	break;
	case 0x0C:	c->t4		= value;	break;
	case 0x0D:	c->t5		= value;	break;
	case 0x0E:	c->t6		= value;	break;
	case 0x0F:	c->t7		= value;	break;
	case 0x10:	c->s0		= value;	break;
	case 0x11:	c->s1		= value;	break;
	case 0x12:	c->s2		= value;	break;
	case 0x13:	c->s3		= value;	break;
	case 0x14:	c->s4		= value;	break;
	case 0x15:	c->s5		= value;	break;
	case 0x16:	c->s6		= value;	break;
	case 0x17:	c->s7		= value;	break;
	case 0x18:	c->t8		= value;	break;
	case 0x19:	c->t9		= value;	break;
	case 0x1C:	c->gp		= value;	break;
	case 0x1D:	c->sp		= value;	break;
	case 0x1E:	c->s8		= value;	break;
	case 0x1F:	c->ra		= value;	break;
	case 0x20:	c->sr		= value;	break;
	case 0x21:	c->lo		= value;	break;
	case 0x22:	c->hi		= value;	break;
	case 0x23:	c->badvaddr	= value;	break;
	case 0x24:	c->cause	= value;	break;
	case 0x25:	c->pc		= value;	break;
	case 0x26:	f[0]		= value;	break;
	case 0x28:	f[1]		= value;	break;
	case 0x2A:	f[2]		= value;	break;
	case 0x2C:	f[3]		= value;	break;
	case 0x2E:	f[4]		= value;	break;
	case 0x30:	f[5]		= value;	break;
	case 0x32:	f[6]		= value;	break;
	case 0x34:	f[7]		= value;	break;
	case 0x36:	f[8]		= value;	break;
	case 0x38:	f[9]		= value;	break;
	case 0x3A:	f[10]		= value;	break;
	case 0x3C:	f[11]		= value;	break;
	case 0x3E:	f[12]		= value;	break;
	case 0x40:	f[13]		= value;	break;
	case 0x42:	f[14]		= value;	break;
	case 0x44:	f[15]		= value;	break;
	case 0x46:	c->fpcsr	= value;	break;
	}
}

/* Address ranges that are exempt from execute breakpoints. These ranges depend
 * on the precise layout of the functions in their respective files and in the
 * spec. If their order changes, these ranges must be changed as well. */
static struct address_range bkp_exempt_ranges[] =
{
	{
		(uintptr_t) &__osPiCreateAccessQueue,
		/*
		(uintptr_t) &__osPiGetAccess,
		(uintptr_t) &__osPiRelAccess,
		(uintptr_t) &osSendMesg,
		(uintptr_t) &osStopThread,
		*/
		(uintptr_t) &osViExtendVStart,
	},

	{
		(uintptr_t) &osRecvMesg,
		/*
		*/
		(uintptr_t) &__createSpeedParam,
	},

	{
		(uintptr_t) &__osEnqueueAndYield,
		/*
		(uintptr_t) &__osEnqueueThread,
		(uintptr_t) &__osPopThread,
		(uintptr_t) &__osNop,
		(uintptr_t) &__osDispatchThread,
		*/
		(uintptr_t) &__osCleanupThread,
	},

	{
		(uintptr_t) &__osDequeueThread,
		/*
		*/
		(uintptr_t) &osDestroyThread,
	},

	{
		(uintptr_t) &__osGetSR,
		/*
		(uintptr_t) &osWritebackDCache,
		*/
		(uintptr_t) &osViGetNextFramebuffer,
	},

	{
		(uintptr_t) &osInvalICache,
		/*
		(uintptr_t) &osCreateMesgQueue,
		(uintptr_t) &osInvalDCache,
		*/
		(uintptr_t) &__osSiDeviceBusy,
	},

	{
		(uintptr_t) &osSetThreadPri,
		/*
		*/
		(uintptr_t) &osGetThreadPri,
	},

	{
		(uintptr_t) &__osDisableInt,
		/*
		(uintptr_t) &__osRestoreInt
		*/
		(uintptr_t) &__osViInit,
	},

	{
		(uintptr_t) &osStartThread,
		/*
		*/
		(uintptr_t) &osViSetYScale,
	},

	{
		(uintptr_t) &__osSetWatchLo,
		/*
		*/
		(uintptr_t) &rspbootTextStart,
	},
};

static int bkp_address_exempt(uintptr_t addr)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(bkp_exempt_ranges); i++)
	{
		struct address_range *range = &bkp_exempt_ranges[i];

		if (addr >= range->start && addr < range->end)
			return 1;
	}

	return 0;
}

static int set_bkp(struct swbkp *bkp, uintptr_t addr)
{
	u32 *p;

	if (bkp->active)
	{
		if (bkp->addr == addr)
			return 0;
		else
			return -1;
	}

	if (!check_addr(addr, 4) || bkp_address_exempt(addr))
		return -1;

	p = (void *) addr;
	bkp->active = 1;
	bkp->addr = addr;
	bkp->old_insn = *p;
	bkp->new_insn = 0x00000034; /* teq $zero, $zero, 0 */

	*p = bkp->new_insn;
	osWritebackDCache(p, 4);
	osInvalICache(p, 4);

	return 0;
}

static int clear_bkp(struct swbkp *bkp)
{
	u32 *p;

	if (bkp->active)
	{
		p = (void *) bkp->addr;
		bkp->active = 0;

		if (*p == bkp->new_insn)
		{
			*p = bkp->old_insn;
			osWritebackDCache(p, 4);
			osInvalICache(p, 4);
		}
	}

	return 0;
}

static void enable_watch(void)
{
	u32 watchlo;

	if (rdb.watch_active)
		watchlo = (rdb.watch_addr & 0x1FFFFFF8) | (rdb.watch_type & 3);
	else
		watchlo = 0;

	__osSetWatchLo(watchlo);
}

static void disable_watch(void)
{
	__osSetWatchLo(0);
}

static int thread_exempt(OSThread *thread)
{
	/* These threads are necessary for the debugger thread to function, so
	 * we don't want to interfere with them. */
	return thread == &rdb_thread
		|| thread == &viThread
		|| thread == &piThread
		|| thread == &sIdleThread
		;
}

static OSThread *first_thread(void)
{
	OSThread *thread;

	for_each_thread(thread)
	{
		return thread;
	}

	return NULL;
}

static OSThread *thread_by_id(OSId thread_id)
{
	OSThread *thread;

	for_each_thread(thread)
	{
		if (thread->id == thread_id)
			return thread;
	}

	return NULL;
}

static void startall(void)
{
	OSThread *thread;

	for_each_thread(thread)
	{
		osStartThread(thread);
	}
}

static void stopall(void)
{
	OSThread *thread;

	for_each_thread(thread)
	{
		osStopThread(thread);
	}
}

static void run(void)
{
	if (!rdb.running)
	{
		rdb.running = 1;

		enable_watch();

		startall();
	}
}

static void stop(void)
{
	if (rdb.running)
	{
		stopall();

		disable_watch();

		rdb.running = 0;

		if (rdb.stepping)
		{
			if (rdb.step_thread->priority == OS_PRIORITY_APPMAX)
				osSetThreadPri(rdb.step_thread, rdb.step_pri);

			clear_bkp(&rdb.step_bkp[0]);
			clear_bkp(&rdb.step_bkp[1]);

			rdb.stepping = 0;
		}
	}
}

static void stop_reply(OSThread *thread)
{
	int watch_hit = 0;
	int sig;

	switch ((thread->context.cause >> 2) & 0x1F)
	{
	case 0:		/* int */
		sig = SIGINT;	break;
	case 10:	/* ri */
		sig = SIGILL;	break;
	case 15:	/* fpe */
		sig = SIGFPE;	break;
	case 2:		/* tlbl */
	case 3:		/* tlbs */
	case 4:		/* adel */
	case 5:		/* ades */
		sig = SIGSEGV;	break;
	case 6:		/* ibe */
	case 7:		/* dbe */
		sig = SIGBUS;	break;
	case 8:		/* sys */
	case 9:		/* bp */
	case 13:	/* tr */
		sig = SIGTRAP;	break;
	case 23:	/* watch */
		if (rdb.watch_active)
			watch_hit = 1;
		sig = SIGTRAP;	break;
	default:
		sig = SIGTRAP;	break;
	}

	pkt_start(0);
	pkt_putc('T');
	pkt_putx_n(sig, 2);

	if (thread->id != 0)
	{
		pkt_puts("thread:");
		pkt_putx(thread->id);
		pkt_putc(';');
	}

	pkt_puts("25:");
	pkt_putx_n(get_reg(thread, 0x25), 16);
	pkt_putc(';');

	if (watch_hit)
	{
		if (rdb.watch_type == 2)
			pkt_putc('r');
		else if (rdb.watch_type == 3)
			pkt_putc('a');
		pkt_puts("watch:");
		pkt_putx(rdb.watch_addr);
		pkt_putc(';');
	}

	if (rdb.libs_changed)
	{
		struct rdb_lib *lib = rdb_changed_lib;

		pkt_puts("library:");

		if (lib != NULL)
		{
			if (lib->load_addr != NULL)
				pkt_putc('+');
			else
				pkt_putc('-');

			if (lib->type == RDB_LIB_OVERLAY)
				pkt_puts("ovl_");
			pkt_puts(lib->name);
			pkt_puts(".o");

			if (lib->load_addr != NULL)
			{
				pkt_puts(",s,");
				pkt_putx((s32) lib->load_addr);
			}
		}

		pkt_putc(';');

		rdb.libs_changed = 0;
	}

	pkt_finish();
}

static void step(OSThread *thread)
{
	struct vr4300_insn insn;
	u32 pc;
	uintptr_t step_addr[2];
	int bkps_set;

	pc = thread->context.pc;

	/* Set stepping breakpoints */
	if (check_addr(pc, 4) && vr4300_decode_insn(*(u32 *) pc, &insn))
	{
		switch (insn.opcode)
		{
		/* 0 operand branch */
		case VR4300_OP_BC1F:
		case VR4300_OP_BC1FL:
		case VR4300_OP_BC1T:
		case VR4300_OP_BC1TL:
			step_addr[0] = pc + 4 + insn.opnd_value[0];
			step_addr[1] = pc + 8;
			break;
		/* 1 operand branch */
		case VR4300_OP_BGEZ:
		case VR4300_OP_BGEZAL:
		case VR4300_OP_BGEZALL:
		case VR4300_OP_BGEZL:
		case VR4300_OP_BGTZ:
		case VR4300_OP_BGTZL:
		case VR4300_OP_BLEZ:
		case VR4300_OP_BLEZL:
		case VR4300_OP_BLTZ:
		case VR4300_OP_BLTZAL:
		case VR4300_OP_BLTZALL:
		case VR4300_OP_BLTZL:
			step_addr[0] = pc + 4 + insn.opnd_value[1];
			step_addr[1] = pc + 8;
			break;
		/* 2 operand branch */
		case VR4300_OP_BEQ:
		case VR4300_OP_BEQL:
		case VR4300_OP_BNE:
		case VR4300_OP_BNEL:
			step_addr[0] = pc + 4 + insn.opnd_value[2];
			step_addr[1] = pc + 8;
			break;
		/* Jump */
		case VR4300_OP_J:
			step_addr[0] = (pc & 0xF0000000) | insn.opnd_value[0];
			step_addr[1] = 0;
			break;
		/* Jump and link */
		case VR4300_OP_JAL:
			/* Stepping into certain functions is forbidden, so
			 * we put a breakpoint on the return address as well
			 * so that we can step over such function calls. */
			step_addr[0] = (pc & 0xF0000000) | insn.opnd_value[0];
			step_addr[1] = pc + 8;
			break;
		/* Register jump and link */
		case VR4300_OP_JALR:
			/* See jump and link */
			step_addr[0] = get_reg(thread, insn.opnd_value[1]);
			step_addr[1] = pc + 8;
			break;
		/* Register jump */
		case VR4300_OP_JR:
			step_addr[0] = get_reg(thread, insn.opnd_value[0]);
			step_addr[1] = 0;
			break;
		/* Other */
		default:
			step_addr[0] = pc + 4;
			step_addr[1] = 0;
			break;
		}
	}
	else
	{
		step_addr[0] = pc + 4;
		step_addr[1] = 0;
	}

	bkps_set = 0;
	if (step_addr[0] != 0 && set_bkp(&rdb.step_bkp[0], step_addr[0]) == 0)
		bkps_set++;
	if (step_addr[1] != 0 && set_bkp(&rdb.step_bkp[1], step_addr[1]) == 0)
		bkps_set++;

	if (bkps_set != 0)
	{
		/* Increase the thread priority while stepping to speed things
		 * up */
		rdb.step_thread = thread;
		rdb.step_pri = thread->priority;
		if (rdb.step_pri < OS_PRIORITY_APPMAX)
			osSetThreadPri(thread, OS_PRIORITY_APPMAX);

		/* Go! */
		rdb.stepping = 1;
		run();
	}
	else
	{
		/* We failed to set any stepping breakpoints, refuse to step */
		rdb.cthread = thread;
		rdb.gthread = thread;

		thread->context.cause = 0; /* SIGINT */
		stop_reply(thread);
	}
}

static void attach(void)
{
	if (!rdb.attached)
	{
		rdb.attached = 1;
		rdb.running = 1;

		stop();

		rdb.cthread = first_thread();
		rdb.gthread = first_thread();
	}
}

static void detach(void)
{
	if (rdb.attached)
	{
		int i;

		disable_watch();

		for (i = 0; i < SWBKP_MAX; ++i)
			clear_bkp(&rdb.swbkp[i]);

		clear_bkp(&rdb.step_bkp[0]);
		clear_bkp(&rdb.step_bkp[1]);

		rdb.attached = 0;
		rdb.noack = 0;
		rdb.running = 0;
		rdb.stepping = 0;

		startall();
	}
}

#define pkt_try_scan(fn) \
	{ \
		int pkt_try_scan_ret__ = pkt_scan_ ## fn; \
		if (pkt_try_scan_ret__ != 0) \
			return pkt_try_scan_ret__; \
	}

static int handle_qSupported(void)
{
	putpkt(0, "QStartNoAckMode+"
		";qXfer:features:read+"
		";qXfer:libraries:read+");

	return 0;
}

static int handle_QStartNoAckMode(void)
{
	pkt_try_scan(eof());

	putpkt(0, "OK");
	rdb.noack = 1;

	return 0;
}

static int handle_qAttached(void)
{
	pkt_try_scan(eof());

	putpkt(0, "1");

	return 0;
}

static int handle_qfThreadInfo(void)
{
	int first;
	OSThread *thread;

	pkt_try_scan(eof());

	pkt_start(0);
	pkt_putc('m');

	first = 1;
	for_each_thread(thread)
	{
		if (first)
			first = 0;
		else
			pkt_putc(',');

		pkt_putx(thread->id);
	}

	pkt_finish();

	return 0;
}

static int handle_qsThreadInfo(void)
{
	pkt_try_scan(eof());

	putpkt(0, "l");

	return 0;
}

static int handle_qC(void)
{
	pkt_try_scan(eof());

	pkt_start(0);
	pkt_puts("QC");
	pkt_putx(rdb.cthread->id);
	pkt_finish();

	return 0;
}

static int handle_H(void)
{
	char op;
	u32 thread_id;
	OSThread **thread_ptr;
	OSThread *thread;

	pkt_try_scan(c(&op));
	if (pkt_scan_lit("-1") == 0)
		thread_id = first_thread()->id;
	else
		pkt_try_scan(x32(&thread_id, 0));
	pkt_try_scan(eof());

	if (op == 'c')
		thread_ptr = &rdb.cthread;
	else if (op == 'g')
		thread_ptr = &rdb.gthread;
	else
		thread_ptr = NULL;

	if (thread_id != 0)
		thread = thread_by_id(thread_id);
	else if (thread_ptr != NULL)
		thread = *thread_ptr;
	else
		thread = NULL;

	if (thread != NULL && thread_ptr != NULL)
	{
		*thread_ptr = thread;

		putpkt(0, "OK");

		return 0;
	}
	else
	{
		return -1;
	}
}

static int handle_T(void)
{
	u32 thread_id;

	if (pkt_scan_lit("-1") == 0)
		thread_id = first_thread()->id;
	else
		pkt_try_scan(x32(&thread_id, 0));
	pkt_try_scan(eof());

	if (thread_by_id(thread_id) != NULL)
	{
		putpkt(0, "OK");

		return 0;
	}
	else
	{
		return -1;
	}
}

static int handle_query_halt(void)
{
	pkt_try_scan(eof());

	stop_reply(rdb.cthread);

	return 0;
}

static int handle_g(void)
{
	int i;

	pkt_try_scan(eof());

	pkt_start(0);
	for (i = 0; i < 0x48; ++i)
		pkt_putx_n(get_reg(rdb.gthread, i), 16);
	pkt_finish();

	return 0;
}

static int handle_G(void)
{
	int i;

	for (i = 0; i < 0x48; ++i)
	{
		u64 v;
		if (pkt_scan_x64(&v, 16) == 0)
			set_reg(rdb.gthread, i, v);
		else
			break;

	}
	pkt_try_scan(eof());

	putpkt(0, "OK");

	return 0;
}

static int handle_p(void)
{
	u32 r;

	pkt_try_scan(x32(&r, 0));
	pkt_try_scan(eof());

	pkt_start(0);
	pkt_putx_n(get_reg(rdb.gthread, r), 16);
	pkt_finish();

	return 0;
}

static int handle_P(void)
{
	u32 r;
	u64 v;

	pkt_try_scan(x32(&r, 0));
	pkt_try_scan(lit("="));
	pkt_try_scan(x64(&v, 16));
	pkt_try_scan(eof());

	set_reg(rdb.gthread, r, v);

	putpkt(0, "OK");

	return 0;
}

static int handle_m(void)
{
	u32 addr;
	u32 length;

	pkt_try_scan(x32(&addr, 0));
	pkt_try_scan(lit(","));
	pkt_try_scan(x32(&length, 0));
	pkt_try_scan(eof());

	if (!check_addr(addr, 1))
		return -1;

	pkt_start(0);
	while (length != 0)
	{
		if (check_addr(addr, 1))
		{
			u8 b = * (u8 *) addr;
			pkt_putx_n(b, 2);

			addr++;
			length--;
		}
		else
		{
			break;
		}
	}
	pkt_finish();

	return 0;
}

static int handle_M(void)
{
	u32 addr;
	u32 length;

	pkt_try_scan(x32(&addr, 0));
	pkt_try_scan(lit(","));
	pkt_try_scan(x32(&length, 0));
	pkt_try_scan(lit(":"));

	while (length != 0)
	{
		if (check_addr(addr, 1))
		{
			u8 b;
			pkt_try_scan(x8(&b, 2));
			* (u8 *) addr = b;

			addr++;
			length--;
		}
		else
		{
			return -1;
		}
	}
	pkt_try_scan(eof());

	putpkt(0, "OK");

	return 0;
}

static int handle_z(void)
{
	char type;
	u32 addr;
	u32 length;

	pkt_try_scan(c(&type));
	pkt_try_scan(lit(","));
	pkt_try_scan(x32(&addr, 0));
	pkt_try_scan(lit(","));
	pkt_try_scan(x32(&length, 0));
	pkt_try_scan(eof());

	if (type == '0')
	{
		if (length != 4)
		{
			return -1;
		}
		else
		{
			int i;
			struct swbkp *b = NULL;

			for (i = 0; i < SWBKP_MAX; ++i)
			{
				struct swbkp *bi = &rdb.swbkp[i];
				if (bi->active && bi->addr == addr)
				{
					b = bi;
					break;
				}
				else if (b == NULL && !bi->active)
				{
					b = bi;
				}
			}

			if (clear_bkp(b) != 0)
				return -1;
		}
	}
	else if (type == '2' || type == '3' || type == '4')
	{
		int t = type - '1';

		if (rdb.watch_active
			&& rdb.watch_addr == addr
			&& rdb.watch_length == length
			&& rdb.watch_type == t)
		{
			rdb.watch_active = 0;
		}
	}
	else
	{
		return -1;
	}

	putpkt(0, "OK");

	return 0;
}

static int handle_Z(void)
{
	char type;
	u32 addr;
	u32 length;

	pkt_try_scan(c(&type));
	pkt_try_scan(lit(","));
	pkt_try_scan(x32(&addr, 0));
	pkt_try_scan(lit(","));
	pkt_try_scan(x32(&length, 0));
	pkt_try_scan(eof());

	if (type == '0')
	{
		if (length != 4)
		{
			return -1;
		}
		else
		{
			int i;
			struct swbkp *b = NULL;

			for (i = 0; i < SWBKP_MAX; ++i)
			{
				struct swbkp *bi = &rdb.swbkp[i];
				if (bi->active && bi->addr == addr)
				{
					b = bi;
					break;
				}
				else if (b == NULL && !bi->active)
				{
					b = bi;
				}
			}

			if (set_bkp(b, addr) != 0)
				return -1;
		}
	}
	else if (type == '2' || type == '3' || type == '4')
	{
		int t = type - '1';

		if (rdb.watch_active)
		{
			if (rdb.watch_addr != addr
				|| rdb.watch_length != length
				|| rdb.watch_type != t)
			{
				return -1;
			}
		}
		else
		{
			rdb.watch_active = 1;
			rdb.watch_addr = addr;
			rdb.watch_length = length;
			rdb.watch_type = t;
		}
	}
	else
	{
		return -1;
	}

	putpkt(0, "OK");

	return 0;
}

static int handle_c(void)
{
	u32 addr = rdb.cthread->context.pc;

	pkt_scan_x32(&addr, 0);
	pkt_try_scan(eof());

	rdb.cthread->context.pc = addr;
	run();

	return 0;
}

static int handle_C(void)
{
	u32 sig;
	u32 addr = rdb.cthread->context.pc;

	pkt_try_scan(x32(&sig, 0));
	if (pkt_scan_lit(";") == 0)
		pkt_try_scan(x32(&addr, 0));
	pkt_try_scan(eof());

	rdb.cthread->context.pc = addr;
	run();

	return 0;
}

static int handle_s(void)
{
	u32 addr = rdb.cthread->context.pc;

	pkt_scan_x32(&addr, 0);
	pkt_try_scan(eof());

	rdb.cthread->context.pc = addr;
	step(rdb.cthread);

	return 0;
}

static int handle_S(void)
{
	u32 sig;
	u32 addr = rdb.cthread->context.pc;

	pkt_try_scan(x32(&sig, 0));
	if (pkt_scan_lit(";") == 0)
		pkt_try_scan(x32(&addr, 0));
	pkt_try_scan(eof());

	rdb.cthread->context.pc = addr;
	step(rdb.cthread);

	return 0;
}

static int handle_D(void)
{
	pkt_try_scan(eof());

	putpkt(0, "OK");
	detach();

	return 0;
}

static int handle_k(void)
{
	pkt_try_scan(eof());

	/* We assume the intent of a kill request is just to disconnect, if
	 * the user really wants to kill the program they'll use the power
	 * switch. */
	detach();

	return 0;
}

static int handle_qXfer_features_read(void)
{
	u32 offset;
	u32 length;

	pkt_try_scan(x32(&offset, 0));
	pkt_try_scan(lit(","));
	pkt_try_scan(x32(&length, 0));
	pkt_try_scan(eof());

	pkt_start(0);
	pkt_putc('l');
	pkt_puts("<target version=\"1.0\">");
	pkt_puts("<architecture>mips:4300</architecture>");
	pkt_puts("</target>");
	pkt_finish();

	return 0;
}

static void xfer_lib(struct rdb_lib *lib)
{
	pkt_puts("<library name=\"");
	if (lib->type == RDB_LIB_OVERLAY)
		pkt_puts("ovl_");
	pkt_puts(lib->name);
	pkt_puts(".o\">");

	pkt_puts("<section address=\"0x");
	pkt_putx((s32) lib->load_addr);
	pkt_puts("\"/>");

	pkt_puts("</library>");

	rdb.lib_num++;
}

static int next_gamestate_lib(struct rdb_lib *lib)
{
	while (rdb.lib_gamestate_pos < ARRAY_COUNT(gGameStateOverlayTable))
	{
		int idx = rdb.lib_gamestate_pos++;
		GameStateOverlay *ovl = &gGameStateOverlayTable[idx];

		if (ovl->loadedRamAddr != NULL)
			return rdb_gamestate_lib(ovl, lib);
	}

	return -1;
}

static int next_kaleido_lib(struct rdb_lib *lib)
{
	while (rdb.lib_kaleido_pos < ARRAY_COUNT(gKaleidoMgrOverlayTable))
	{
		int idx = rdb.lib_kaleido_pos++;
		KaleidoMgrOverlay *ovl = &gKaleidoMgrOverlayTable[idx];

		if (ovl->loadedRamAddr != NULL)
			return rdb_kaleido_lib(ovl, lib);
	}

	return -1;
}

static int next_actor_lib(struct rdb_lib *lib)
{
	while (rdb.lib_actor_pos < ARRAY_COUNT(gActorOverlayTable))
	{
		int idx = rdb.lib_actor_pos++;
		ActorOverlay *ovl = &gActorOverlayTable[idx];

		if (ovl->loadedRamAddr != NULL)
			return rdb_actor_lib(ovl, lib);
	}

	return -1;
}

static int next_effect_lib(struct rdb_lib *lib)
{
	while (rdb.lib_effect_pos < ARRAY_COUNT(gEffectSsOverlayTable))
	{
		int idx = rdb.lib_effect_pos++;
		EffectSsOverlay *ovl = &gEffectSsOverlayTable[idx];

		if (ovl->loadedRamAddr != NULL)
			return rdb_effect_lib(ovl, lib);
	}

	return -1;
}

static int handle_qXfer_libraries_read(void)
{
	struct rdb_lib libs[MAX_XFER_LIBS];
	int num_libs;

	int i;

	u32 offset;
	u32 length;

	pkt_try_scan(x32(&offset, 0));
	pkt_try_scan(lit(","));
	pkt_try_scan(x32(&length, 0));
	pkt_try_scan(eof());

	if (offset == 0)
	{
		rdb.lib_num = 0;
		rdb.lib_gamestate_pos = 0;
		rdb.lib_kaleido_pos = 0;
		rdb.lib_actor_pos = 0;
		rdb.lib_effect_pos = 0;
	}

	num_libs = 0;
	while (num_libs < MAX_XFER_LIBS)
	{
		struct rdb_lib *lib = &libs[num_libs];

		if (next_gamestate_lib(lib) == 0
			|| next_kaleido_lib(lib) == 0
			|| next_actor_lib(lib) == 0
			|| next_effect_lib(lib) == 0)
		{
			num_libs++;
		}
		else
		{
			break;
		}
	}

	pkt_start(0);

	if (num_libs < MAX_XFER_LIBS)
		pkt_putc('l');
	else
		pkt_putc('m');

	if (rdb.lib_num == 0)
		pkt_puts("<library-list>");

	for (i = 0; i < num_libs; i++)
	{
		struct rdb_lib *lib = &libs[i];

		xfer_lib(lib);
	}

	if (num_libs < MAX_XFER_LIBS)
		pkt_puts("</library-list>");

	pkt_finish();

	return 0;
}

static struct pkt_handler pkt_handlers[] =
{
	{ "qSupported",				handle_qSupported },
	{ "QStartNoAckMode",			handle_QStartNoAckMode },
	{ "qAttached",				handle_qAttached },
	{ "qfThreadInfo",			handle_qfThreadInfo },
	{ "qsThreadInfo",			handle_qsThreadInfo },
	{ "qC",					handle_qC },
	{ "H",					handle_H },
	{ "T",					handle_T },
	{ "?",					handle_query_halt },
	{ "g",					handle_g },
	{ "G",					handle_G },
	{ "p",					handle_p },
	{ "P",					handle_P },
	{ "m",					handle_m },
	{ "M",					handle_M },
	{ "z",					handle_z },
	{ "Z",					handle_Z },
	{ "c",					handle_c },
	{ "C",					handle_C },
	{ "s",					handle_s },
	{ "S",					handle_S },
	{ "D",					handle_D },
	{ "k",					handle_k },
	{ "qXfer:features:read:target.xml:",	handle_qXfer_features_read },
	{ "qXfer:libraries:read::",		handle_qXfer_libraries_read }
};

static void handle_pkt(int notif)
{
	int i;
	int handled;

	if (getpkt(notif) == 0)
	{
		/* If we received a valid packet, attach the debugger. We are
		 * now in a live debugging session. */
		attach();
	}
	else
	{
		return;
	}

	handled = 0;
	for (i = 0; i < ARRAY_COUNT(pkt_handlers); i++)
	{
		struct pkt_handler *h = &pkt_handlers[i];

		if (pkt_scan_lit(h->name) == 0)
		{
			if (h->handler() != 0)
			{
				putpkt(0, "E00");
			}

			handled = 1;
			break;
		}
	}

	/* Unrecognized packet, send an empty response */
	if (!handled)
	{
		pkt_start(0);
		pkt_finish();
	}
}

static void handle_fifo(void)
{
	do
	{
		switch (rx_getc())
		{
		case '$':
			/* We got a packet introduction, try to receive and
			 * handle the packet. */
			handle_pkt(0);
			break;

		case '\003':
			/* CTRL-C, do an interrupt. We only want to do this
			 * once we've established a connection though, else we
			 * don't know where this character came from or what
			 * the intent is. */
			if (rdb.attached && rdb.running)
			{
				stop();

				rdb.cthread->context.cause = 0; /* SIGINT */
				stop_reply(rdb.cthread);
			}
			break;
		}
	}
	while (rx_poll());
}

static void handle_thread_event(void)
{
	if (rdb.running)
	{
		stop();

		rdb.cthread = __osFaultedThread;
		rdb.gthread = __osFaultedThread;

		stop_reply(rdb.cthread);
	}
}

static void handle_lib_event(void)
{
	if (rdb.attached)
	{
		rdb.libs_changed = 1;

		if (rdb.running)
		{
			stop();

			rdb.cthread->context.cause = 0; /* SIGINT */
			stop_reply(rdb.cthread);
		}
	}
}

static void rdb_main(void *arg)
{
	io_init();

	bzero(&rdb, sizeof(rdb));
	disable_watch();

	osCreateMesgQueue(&rdb_mq, rdb_mesg, ARRAY_COUNT(rdb_mesg));

	/* Catch thread events */
	osSetEventMesg(OS_EVENT_CPU_BREAK, &rdb_mq,
			(OSMesg) OS_EVENT_CPU_BREAK);
	osSetEventMesg(OS_EVENT_FAULT, &rdb_mq,
			(OSMesg) OS_EVENT_FAULT);

	for (;;)
	{
		unsigned long fifo_irqs;

		u32 m;

		/* Wait for something to happen */
		/* fifo_sleep/spin blocks until a message is received on the
		 * specified queue, or there's an event on the FIFO. */

		/* We always want to check for data on the FIFO */
		fifo_irqs = FIFO_IRQ_RX;

		/* If we're connected, also check for a disconnect */
		if (rdb.attached)
			fifo_irqs |= FIFO_IRQ_OFF;

		/* If we're detached or running, sleep to allow other threads
		 * to run, otherwise spin to improve responsiveness in the
		 * debugger. */
		if (!rdb.attached || rdb.running)
			m = (u32) fifo_sleep(&rdb_mq, fifo_irqs);
		else
			m = (u32) fifo_spin(&rdb_mq, fifo_irqs);

		switch (m)
		{
		case OS_EVENT_FIFO_OFF:
			/* FIFO disconnected */
			detach();
			break;

		case OS_EVENT_FIFO_RX:
			/* We got some data on the FIFO */
			handle_fifo();
			break;

		case OS_EVENT_CPU_BREAK:
		case OS_EVENT_FAULT:
			/* A thread event occurred */
			handle_thread_event();
			break;

		case RDB_EVENT_LIB:
			/* Libraries have changed */
			handle_lib_event();
			break;
		}
	}
}

void rdb_start(void)
{
	osCreateThread(&rdb_thread, 0, rdb_main, NULL,
			&rdb_stack[ARRAY_COUNT(rdb_stack) - 2],
			OS_PRIORITY_RMON);
	osStartThread(&rdb_thread);
}

int rdb_gamestate_lib(void *ovl_struct, struct rdb_lib *lib)
{
	static const char *lib_name_tbl[] =
	{
		NULL,
		"select",
		"title",
		NULL,
		"opening",
		"file_choose",
	};

	GameStateOverlay *ovl = ovl_struct;

	if (ovl->vramStart == NULL)
	{
		return -1;
	}
	else
	{
		int idx = ovl - gGameStateOverlayTable;

		lib->type = RDB_LIB_OVERLAY;
		lib->name = lib_name_tbl[idx];
		lib->vram_start = ovl->vramStart;
		lib->vram_end = ovl->vramEnd;
		lib->load_addr = ovl->loadedRamAddr;

		return 0;
	}
}

int rdb_kaleido_lib(void *ovl_struct, struct rdb_lib *lib)
{
	KaleidoMgrOverlay *ovl = ovl_struct;

	if (ovl->vramStart == NULL)
	{
		return -1;
	}
	else
	{
		int idx = ovl - gKaleidoMgrOverlayTable;

		lib->type = RDB_LIB_OVERLAY;
		lib->name = ovl->name;
		lib->vram_start = ovl->vramStart;
		lib->vram_end = ovl->vramEnd;
		lib->load_addr = ovl->loadedRamAddr;

		return 0;
	}
}

int rdb_actor_lib(void *ovl_struct, struct rdb_lib *lib)
{
	ActorOverlay *ovl = ovl_struct;

	if (ovl->vramStart == NULL)
	{
		return -1;
	}
	else
	{
		int idx = ovl - gActorOverlayTable;

		lib->type = RDB_LIB_OVERLAY;
		lib->name = ovl->name;
		lib->vram_start = ovl->vramStart;
		lib->vram_end = ovl->vramEnd;
		lib->load_addr = ovl->loadedRamAddr;

		return 0;
	}
}

int rdb_effect_lib(void *ovl_struct, struct rdb_lib *lib)
{
	static const char *lib_name_tbl[] =
	{
#define DEFINE_EFFECT_SS(name, num) #name,
#define DEFINE_EFFECT_SS_UNSET(num) NULL,
#include <tables/effect_ss_table.h>
#undef DEFINE_EFFECT_SS
#undef DEFINE_EFFECT_SS_UNSET
	};

	EffectSsOverlay *ovl = ovl_struct;

	if (ovl->vramStart == NULL)
	{
		return -1;
	}
	else
	{
		int idx = ovl - gEffectSsOverlayTable;

		lib->type = RDB_LIB_OVERLAY;
		lib->name = lib_name_tbl[idx];
		lib->vram_start = ovl->vramStart;
		lib->vram_end = ovl->vramEnd;
		lib->load_addr = ovl->loadedRamAddr;

		return 0;
	}
}

void rdb_lib_changed(void *ovl_struct, rdb_lib_fn *lib_fn)
{
	int irqf = __osDisableInt();

	if (ovl_struct != NULL)
	{
		static struct rdb_lib lib;

		(*lib_fn)(ovl_struct, &lib);

		rdb_changed_lib = &lib;
	}
	else
	{
		rdb_changed_lib = NULL;
	}

	osSendMesg(&rdb_mq, (OSMesg) RDB_EVENT_LIB, OS_MESG_NOBLOCK);

	__osRestoreInt(irqf);
}
