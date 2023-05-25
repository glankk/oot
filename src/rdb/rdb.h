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

#ifndef RDB_H
#define RDB_H

#define RDB_LIB_STATIC	0
#define RDB_LIB_OVERLAY	1

struct rdb_lib
{
	int		type;
	const char *    name;
	void *		vram_start;
	void *		vram_end;
	void *		load_addr;
};

typedef int rdb_lib_fn(void *ovl_struct, struct rdb_lib *lib);

void rdb_start(void);

int rdb_gamestate_lib(void *ovl_struct, struct rdb_lib *lib);
int rdb_kaleido_lib(void *ovl_struct, struct rdb_lib *lib);
int rdb_actor_lib(void *ovl_struct, struct rdb_lib *lib);
int rdb_effect_lib(void *ovl_struct, struct rdb_lib *lib);

void rdb_lib_changed(void *ovl_struct, rdb_lib_fn *lib_fn);

#endif
