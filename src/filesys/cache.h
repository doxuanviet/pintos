#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#define CACHE_LIMIT 64

struct cache_data
{
	void *addr;								/* Physical address of the cached sector. */
	bool accessed;
	bool dirty;
	int open_cnt;
	block_sector_t sector_id;				/* Which sector is stored here. */
};

struct cache_data cache[CACHE_LIMIT];

struct lock cache_lock;

void cache_init(void);
int cache_load(block_sector_t sector_id);
void cache_flush_all(void);

#endif /* filesys/cache.h */