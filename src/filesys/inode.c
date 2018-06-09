#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define MAX_FILE_SIZE 8000000

#define DIRECT_LIMIT 100
#define DIRECT_SIZE_LIMIT 512000

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  off_t length;                       /* File size in bytes. */
  unsigned magic;                     /* Magic number. */
  /* The rest will be pointers to data:
  [0 -> DIRECT_LIMIT) : direct data.
  DIRECT_LIMIT : doubly indirect data.
  rest: unused. */
  int ptr[128-2];
};

struct indirect_sector
{
  int ptr[128];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock inode_lock;
    struct inode_disk data;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if(pos >= inode->data.length) return -1;

  // Answer is within direct data.
  if(pos < DIRECT_SIZE_LIMIT)
    return inode->data.ptr[pos / 512];

  // Answer is within doubly indirect data.
  pos -= DIRECT_SIZE_LIMIT;
  struct indirect_sector tmp;
  // Read doubly indirect sector.
  block_read(fs_device, inode->data.ptr[DIRECT_LIMIT], &tmp);
  // Read indirect sector.
  block_read(fs_device, tmp.ptr[pos/(128 * 512)], &tmp);
  pos %= (128 * 512);
  return tmp.ptr[pos / 512];
}

// Estimate how many sectors need to be allocate to expand an inode to given length.
int estimate_expand(struct inode *ind, int length)
{
  int cur_sector = bytes_to_sectors(ind->data.length);
  int target_sector = bytes_to_sectors(length);
  if(cur_sector >= target_sector) return 0;

  // Direct data sectors.
  int ans = target_sector - cur_sector;
  if(target_sector <= DIRECT_LIMIT) return ans;
  
  if(cur_sector <= DIRECT_LIMIT)
  {
    ans++;
    cur_sector = DIRECT_LIMIT;
  }
  int cur_indirect_id = (cur_sector - DIRECT_LIMIT) / 128;
  int target_indirect_id = (target_sector - DIRECT_LIMIT) / 128;
  return ans + target_indirect_id - cur_indirect_id;
}

/* Expand an inode to given length. Allocate on-disk memory as needed.
   Also update to on-disk inode. Return true if successful. */
bool inode_expand(struct inode *ind, int length)
{
  printf("Expanding %d to %d\n",ind->data.length, length);
  if(estimate_expand(ind, length) > free_map_free_space()) return false;
  printf("Enough space\n");

  int cur_sector = bytes_to_sectors(ind->data.length);
  int target_sector = bytes_to_sectors(length);
  if(target_sector <= cur_sector) return true;

  int zeroes[BLOCK_SECTOR_SIZE];
  memset(zeroes, 0, sizeof zeroes);

  ind->data.length = length;
  printf("Allocating directly.\n");
  // Direct allocation.
  cur_sector++;
  for(; cur_sector<=DIRECT_LIMIT; cur_sector++)
  {
    free_map_allocate(1, &ind->data.ptr[cur_sector - 1]);
    block_write(fs_device, ind->data.ptr[cur_sector - 1], zeroes);
    if(cur_sector == target_sector)
      {
        block_write(fs_device, ind->sector, &ind->data);
        return true;
      }
  }
  printf("Allocating double indirectly.\n");

  // Doubly indirect allocation.
  if(cur_sector == DIRECT_LIMIT + 1)
    free_map_allocate(1, &ind->data.ptr[DIRECT_LIMIT]);

  struct indirect_sector doubly_indirect, cur_indirect;
  block_read(fs_device, ind->data.ptr[DIRECT_LIMIT], &doubly_indirect);
  int old_data_id = -1;
  for(; ; cur_sector++)
  {
    int data_id = (cur_sector - DIRECT_LIMIT - 1)/128;
    int indirect_id = (cur_sector - DIRECT_LIMIT - 1)%128;
    if(indirect_id == 0)
      free_map_allocate(fs_device, &doubly_indirect.ptr[data_id]);
    if(old_data_id != data_id)
    {
      if(old_data_id != -1)
        block_write(fs_device, doubly_indirect.ptr[old_data_id], &cur_indirect);
      block_read(fs_device, doubly_indirect.ptr[data_id], &cur_indirect);
    }

    free_map_allocate(1, &cur_indirect.ptr[indirect_id]);
    block_write(fs_device, cur_indirect.ptr[indirect_id], zeroes);
    old_data_id = data_id;
    if(cur_sector == target_sector)
    {
      block_write(fs_device, doubly_indirect.ptr[data_id], &cur_indirect);
      block_write(fs_device, ind->data.ptr[DIRECT_LIMIT], &doubly_indirect);
      block_write(fs_device, ind->sector, &ind->data);
      return true;
    }
  }
  // Not reached.
  return true;
}

/* Free all the on-disk data of an inode. */
void inode_free(struct inode *ind)
{
  int cur_sector = bytes_to_sectors(ind->data.length);
  int i;
  // Free direct data.
  for(i=1; i<=DIRECT_LIMIT; i++)
  {
    free_map_release(ind->data.ptr[i - 1], 1);
    if(i == cur_sector) return;
  }
  // Free doubly indirect data.
  struct indirect_sector doubly_indirect, cur_indirect;
  block_read(fs_device, ind->data.ptr[DIRECT_LIMIT], &doubly_indirect);
  int old_data_id = -1;
  for(i=DIRECT_LIMIT+1; ; i++)
  {
    int data_id = (i - DIRECT_LIMIT - 1)/128;
    int indirect_id = (i - DIRECT_LIMIT - 1)%128;
    if(old_data_id != data_id)
    {
      if(old_data_id != -1)
        free_map_release(doubly_indirect.ptr[old_data_id], 1);
      block_read(fs_device, doubly_indirect.ptr[data_id], &cur_indirect);
    }
    free_map_release(cur_indirect.ptr[indirect_id], 1);
    if(i == cur_sector)
    {
      free_map_release(doubly_indirect.ptr[data_id], 1);
      free_map_release(ind->data.ptr[DIRECT_LIMIT], 1);
      return;
    }
    old_data_id = data_id;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  ASSERT (length >= 0);
  struct inode *tmp = malloc(sizeof(struct inode));
  tmp->sector = sector;
  tmp->data.length = 0;
  if(!inode_expand(tmp, length))
  {
    free(tmp);
    return false;
  }
  free(tmp);
  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->inode_lock);
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
      {
        free_map_release (inode->sector, 1);
        inode_free(inode);
      }
      else
        block_write(fs_device, inode->sector, &inode->data);
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  // printf("NEW READ\n");
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  if(offset >= inode->data.length) return 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      int cache_id = cache_load(sector_idx);
      cache[cache_id].accessed = true;
      cache[cache_id].open_cnt--;
      memcpy (buffer + bytes_read, cache[cache_id].addr + sector_ofs, chunk_size);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  // printf("DONE READ\n");
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  // printf("NEW WRITE\n");
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  if(offset + size >= inode->data.length)
    ASSERT(inode_expand(inode, offset + size + 1));

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      int cache_id = cache_load(sector_idx);
      cache[cache_id].accessed = true;
      cache[cache_id].dirty = true;
      cache[cache_id].open_cnt--;
      memcpy(cache[cache_id].addr + sector_ofs, buffer + bytes_written, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  // printf("END WRITE\n");
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
