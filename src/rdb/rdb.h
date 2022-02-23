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
