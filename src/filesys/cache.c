#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"

void cache_init()
{
	int i,j;
	for(i=0; i<CACHE_LIMIT; i+=4)
	{
		void *addr = malloc(BLOCK_SECTOR_SIZE * 4);
		for(j=i; j<i+4; j++)
		{
			cache[j].addr = addr;
			cache[j].accessed = false;
			cache[j].dirty = false;
			cache[j].sector_id = -1;
			addr += BLOCK_SECTOR_SIZE; 
		}
	}

	lock_init(&cache_lock);
	printf("Finish Cache initialization!\n");
}

void cache_flush_out(int cache_id)
{
	if(cache[cache_id].sector_id == -1) return;
	if(cache[cache_id].dirty == true)
		block_write(fs_device, cache[cache_id].sector_id, cache[cache_id].addr);
}

void *cache_evict()
{
	// Cache eviction using second chance algorithm.
	int i_;
	for(i_ = 0; i_ < 2*CACHE_LIMIT; i_++)
	{
		int i = i_ % CACHE_LIMIT;
		if(cache[i].accessed == true)
			cache[i].accessed = false;
		else // Found one to evict.
		{
			cache_flush_out(i);
			return i;
		}
	}
}

int cache_load(block_sector_t sector_id)
{
	lock_acquire(&cache_lock);

	int i;
	for(i=0; i<CACHE_LIMIT; i++)
		if(cache[i].sector_id == sector_id)
		{
			cache[i].accessed = true;
			lock_release(&cache_lock);
			return cache[i].addr;
		}

	int cache_id = cache_evict();
	cache[cache_id].sector_id = sector_id;
	cache[cache_id].accessed = false;
	cache[cache_id].dirty = false;
	block_read (fs_device, sector_id, cache[cache_id].addr);
	lock_release(&cache_lock);
	return cache_id;
}

void cache_flush_all()
{
	lock_acquire(&cache_lock);
	int i;
	for(i=0; i<CACHE_LIMIT; i++) cache_flush_out(i);
	lock_release(&cache_lock);
}