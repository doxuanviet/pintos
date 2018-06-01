#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "process.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);

void halt (void);
void exit(int status);
int exec(const char * cmd_line);
int wait (int pid);
bool create (const char * file , unsigned initial_size );
bool remove (const char * file );
int open (const char * file );
int filesize (int fd );
int read (int fd , void * buffer , unsigned size );
int write (int fd , const void * buffer , unsigned size );
void seek (int fd , unsigned position );
unsigned tell (int fd );
void close (int fd );

struct lock filesys_lock;

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void check_valid(void *ptr)
{
  void *usr_min_addr = 0x08048000;
  if(!is_user_vaddr(ptr) || ptr < usr_min_addr) exit(-1);
  int *cur_pd = thread_current()->pagedir;
  if(!pagedir_get_page(cur_pd, ptr)) exit(-1);
  return 0;
}

void check_valid_str(char *ptr)
{
  check_valid(ptr);
  while(*ptr != '\0')
    check_valid(++ptr);
}

void check_valid_buffer(char *ptr, int size)
{
  while(size--) check_valid(ptr++);
}

int get_arg(void *esp)
{
  check_valid(esp);
  check_valid(esp + 1);
  check_valid(esp + 2);
  check_valid(esp + 3);
  return *(int *)esp;
}
void get_args(void *esp, int *args, int cnt)
{
  while(cnt--)
  {
    *args++ = get_arg(esp);
    esp += 4;
  }
}

static void
syscall_handler (struct intr_frame *f) 
{
  int args[4];
  void *esp = f->esp;
  int call_num = get_arg(esp);
  esp += 4;
  if(call_num == SYS_HALT)
  {
    halt();
  }
  else if(call_num == SYS_EXIT)
  {
    get_args(esp, args, 1);
    exit(args[0]);
  }
  else if(call_num == SYS_EXEC)
  {
    get_args(esp, args, 1);
    check_valid_str(args[0]);
    f->eax = exec(args[0]);
  }
  else if(call_num == SYS_WAIT)
  {
    get_args(esp, args, 1);
    f->eax = wait(args[0]);
  }
  else if(call_num == SYS_CREATE)
  {
    get_args(esp, args, 2);
    check_valid_str(args[0]);
    f->eax = create(args[0], args[1]);
  }
  else if(call_num == SYS_REMOVE)
  {
    get_args(esp, args, 1);
    check_valid_str(args[0]);
    f->eax = remove(args[0]);
  }
  else if(call_num == SYS_OPEN)
  {
    get_args(esp, args, 1);
    check_valid_str(args[0]);
    f->eax = open(args[0]);
  }
  else if(call_num == SYS_FILESIZE)
  {
    get_args(esp, args, 1);
    f->eax = filesize(args[0]);
  }
  else if(call_num == SYS_READ)
  {
    get_args(esp, args, 3);
    check_valid_buffer(args[1], args[2]);
    f->eax = read(args[0], args[1], args[2]);
  }
  else if(call_num == SYS_WRITE)
  {
    get_args(esp, args, 3);
    check_valid_buffer(args[1], args[2]);
    f->eax = write(args[0], args[1], args[2]);
  }
  else if(call_num == SYS_SEEK)
  {
    get_args(esp, args, 2);
    seek(args[0], args[1]);
  }
  else if(call_num == SYS_TELL)
  {
    get_args(esp, args, 1);
    f->eax = tell(args[0]);
  }
  else if(call_num == SYS_CLOSE)
  {
    get_args(esp, args, 1);
    close(args[0]);
  }
  else
  {
  	printf("Not known (yet) syscall.\n");
  	thread_exit ();
  }
}

void halt (void)
{
  shutdown_power_off();
}
void exit(int status)
{
  struct thread *cur = thread_current();
  printf("%s: exit(%d)\n", cur->name, status);
  struct child_process *child = cur->child;
  child->exit_retval = status;
  child->exit_status = 1;
  thread_exit();
}
int exec(const char *cmd_line)
{
  tid_t child_tid = process_execute(cmd_line);
  struct child_process *child = process_get_child(child_tid);
  if(child == NULL) return -1;
  // Not loaded yet
  if(child->load_status == -1)
    sema_down(&child->load_sema);
  // Load fail
  if(child->load_status == 1)
  {
    process_remove_child(child_tid);
    return -1;
  }
  return child_tid;
}
int wait (int pid)
{
  return process_wait(pid);
}
bool create (const char * file , unsigned initial_size )
{
  lock_acquire(&filesys_lock);
  bool res = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return res;
}
bool remove (const char * file )
{
  lock_acquire(&filesys_lock);
  bool res = filesys_remove(file);
  lock_release(&filesys_lock);
  return res;
}
int open (const char * file )
{
  lock_acquire(&filesys_lock);
  struct file *f = filesys_open(file);
  if(f == NULL)
  {
    lock_release(&filesys_lock);
    return -1;
  }
  struct file_descriptor *file_desc = process_add_fd(f);
  lock_release(&filesys_lock);
  return file_desc->fd;
}
int filesize (int fd )
{
  struct file_descriptor *file_desc = process_get_fd(fd);
  if(!file_desc) return -1;
  lock_acquire(&filesys_lock);
  int res = file_length(file_desc->file);
  lock_release(&filesys_lock);
  return res;
}
int read (int fd , void * buffer , unsigned size )
{
  if(fd == STDOUT_FILENO) return 0;
  if(fd == STDIN_FILENO)
  {
    char *buff = (char *) buffer;
    int i;
    for(i=0; i<size; i++)
      buff[i] = input_getc();
    return size;
  }

  struct file_descriptor *file_desc = process_get_fd(fd);
  if(!file_desc) return 0;
  lock_acquire(&filesys_lock);
  int bytes_read = file_read(file_desc->file, buffer, size);
  lock_release(&filesys_lock);
  return bytes_read;
}
int write (int fd , const void * buffer , unsigned size )
{
  if (fd == STDOUT_FILENO)
  {
    putbuf(buffer, size);
    return size;
  }
  struct file_descriptor *file_desc = process_get_fd(fd);
  if(!file_desc) return -1;
  lock_acquire(&filesys_lock);
  int bytes_written = file_write(file_desc->file, buffer, size);
  lock_release(&filesys_lock);
  return bytes_written;
}
void seek (int fd , unsigned position )
{
  struct file_descriptor *file_desc = process_get_fd(fd);
  if(!file_desc) return -1;
  lock_acquire(&filesys_lock);
  file_seek(file_desc->file, position);
  lock_release(&filesys_lock);
}
unsigned tell (int fd )
{
  struct file_descriptor *file_desc = process_get_fd(fd);
  if(!file_desc) return -1;
  lock_acquire(&filesys_lock);
  int res = file_tell(file_desc->file);
  lock_release(&filesys_lock);
  return res;
}
void close (int fd )
{
  struct file_descriptor *file_desc = process_get_fd(fd);
  if(!file_desc) return -1;
  process_remove_fd(fd);
}