#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"

void cache_init()
{
	int i;
	for(i=0; i<CACHE_LIMIT; i++)
	{
		cache[i].addr = malloc(BLOCK_SECTOR_SIZE);
		cache[i].accessed = false;
		cache[i].dirty = false;
		cache[i].open_cnt = 0;
		cache[i].sector_id = -1;
		// printf("%d at addr: %p\n", i, cache[i].addr);
	}

	lock_init(&cache_lock);
	// printf("Finish Cache initialization!\n");
}

void cache_flush_out(int cache_id)
{
	if(cache[cache_id].sector_id == -1) return;
	if(cache[cache_id].dirty == true)
		block_write(fs_device, cache[cache_id].sector_id, cache[cache_id].addr);
}

void *cache_evict()
{
	int i_;
	for(i_ = 0; i_ < CACHE_LIMIT; i_++)
			if(cache[i_].open_cnt == 0) return i_;

	// Cache eviction using second chance algorithm.
	for(i_ = 0; i_ < 2*CACHE_LIMIT; i_++)
	{
		int i = i_ % CACHE_LIMIT;
		if(cache[i].open_cnt > 0) continue;
		if(cache[i].accessed == true)
			cache[i].accessed = false;
		else // Found one to evict.
		{
			cache_flush_out(i);
			return i;
		}
	}
	return -1;
}

int cache_load(block_sector_t sector_id)
{
	lock_acquire(&cache_lock);

	int i;
	for(i=0; i<CACHE_LIMIT; i++)
		if(cache[i].sector_id == sector_id)
		{
			cache[i].accessed = true;
			cache[i].open_cnt++;
			lock_release(&cache_lock);
			return i;
		}

	int cache_id = cache_evict();
	// printf("Evict result %d\n",cache_id);
	cache[cache_id].sector_id = sector_id;
	cache[cache_id].accessed = false;
	cache[cache_id].dirty = false;
	cache[i].open_cnt++;
	block_read (fs_device, sector_id, cache[cache_id].addr);
	lock_release(&cache_lock);
	// printf("Load cache at %p\n",cache[cache_id].addr);
	return cache_id;
}

void cache_flush_all()
{
	lock_acquire(&cache_lock);
	int i;
	for(i=0; i<CACHE_LIMIT; i++) cache_flush_out(i);
	lock_release(&cache_lock);
}