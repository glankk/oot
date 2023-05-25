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

#ifndef RDB_PRIVATE_H
#define RDB_PRIVATE_H

#include <ultra64.h>

#define SIGINT		2
#define SIGILL		4
#define SIGTRAP		5
#define SIGFPE		8
#define SIGBUS		10
#define SIGSEGV		11

#define RDB_EVENT_LIB	(42764 + 1)

#define IO_BUFSIZE	0x600

#define SWBKP_MAX	16

#define MAX_XFER_LIBS	15

#define for_each_thread(thread) \
	for \
	( \
		thread = __osActiveQueue; \
		thread->priority != -1; \
		thread = thread->tlnext \
	) \
	if (thread_exempt(thread) == 0)

enum pkt_type
{
	PKT_NONE,
	PKT_NORM,
	PKT_NOTIF
};

struct address_range
{
	uintptr_t	start;
	uintptr_t	end;
};

struct swbkp
{
	int		active;
	uintptr_t	addr;
	u32		old_insn;
	u32		new_insn;
};

struct pkt_handler
{
	const char *	name;
	int (*		handler)(void);
};

struct rdb
{
	char		rx_buf[IO_BUFSIZE];
	size_t		rx_size;
	size_t		rx_pos;

	char		tx_buf[IO_BUFSIZE];
	size_t		tx_size;

	size_t		ipkt_size;
	size_t		ipkt_pos;

	enum pkt_type	opkt_type;
	u8		opkt_csum;
	int		opkt_rle_len;
	char		opkt_rle_chr;

	int		attached;
	int		noack;
	int		running;
	int		stepping;
	int		libs_changed;

	OSThread *	cthread;
	OSThread *	gthread;

	struct swbkp	swbkp[SWBKP_MAX];

	struct swbkp	step_bkp[2];
	OSThread *	step_thread;
	OSPri		step_pri;

	int		watch_active;
	uintptr_t	watch_addr;
	size_t		watch_length;
	int		watch_type;

	int		lib_num;
	int		lib_gamestate_pos;
	int		lib_kaleido_pos;
	int		lib_actor_pos;
	int		lib_effect_pos;
};

void __createSpeedParam(void);

extern OSThread viThread;
extern OSThread piThread;
extern OSThread sIdleThread;

#endif
