#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  cache_init();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_flush_all();
}

/* Get the parent directory of this directory.
   If it is root already, return root. */
struct dir *get_parent_dir(const char *name, char **file_name)
{
  if(name == NULL) return NULL;
  char *cur_name, full_name[strlen(name) + 1], *save_ptr;
  memcpy(full_name, name, strlen(name) + 1);

  struct dir *cur_dir = NULL;

  if(full_name[0] == '/' || thread_current()->current_dir == NULL)
    cur_dir = dir_open_root();
  else cur_dir = dir_reopen(thread_current()->current_dir);

  cur_name = strtok_r(full_name, "/", &save_ptr);
  while(cur_name != NULL)
  {
    *file_name = strtok_r(NULL, "/", &save_ptr);
    if(*file_name == NULL)
    {
      *file_name = malloc(strlen(cur_name) + 1);
      memcpy(*file_name, cur_name, strlen(cur_name) + 1);
      return cur_dir;
    }

    if(strcmp(cur_name, "..") == 0) cur_dir = dir_go_up(cur_dir);
    else cur_dir = dir_go_down(cur_dir, cur_name);
    if(cur_dir == NULL) return NULL;

    cur_name = *file_name;
  }
  return cur_dir;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  block_sector_t inode_sector = 0;
  char *file_name = NULL;
  struct dir *dir = get_parent_dir(name, &file_name);
  bool success = (dir != NULL && file_name != NULL
                  && strcmp(file_name, ".") && strcmp(file_name, "..")
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free(file_name);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if(strlen(name) == 0) return NULL;
  char *file_name = NULL;
  struct dir *dir = get_parent_dir(name, &file_name);
  if(dir == NULL) return NULL;
  if(file_name == NULL)
  {
    printf("Yes, here\n");
    if((inode_get_inumber(dir_get_inode(dir))) == ROOT_DIR_SECTOR)
      return dir;
    dir_close(dir);
    return NULL;
  }

  if(strcmp(file_name, "..") == 0)
  {
    dir = dir_go_up(dir);
    free(file_name);
    return dir;
  }

  if(strcmp(file_name, ".") == 0) return dir;

  struct inode *ind = NULL;
  dir_lookup (dir, file_name, &ind);
  dir_close (dir);
  free(file_name);

  if(ind == NULL) return NULL;
  if(inode_isdir(ind)) return dir_open (ind);
  return file_open(ind);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char *file_name;
  struct dir *dir = get_parent_dir(name, &file_name);
  if(dir == NULL) return false;

  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir);
  free(file_name);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
