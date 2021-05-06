#include "userprog/syscall.h"
#include <stdio.h>
#include <stdbool.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/inode.h"
#include "devices/input.h"


// Process identifier
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

// Write values
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define MAX_BUFFER 300

// Method declarations
static void syscall_handler (struct intr_frame *);
static void check_pointer (void *ptr);
static void check_arguments (struct intr_frame *f, int num_args);
static void syscall_halt (void);
static void syscall_exit (int status);
static pid_t syscall_exec (const char *cmd_line);
static int syscall_wait (pid_t pid);
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, const void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);
static int syscall_write_helper (const void *buffer, unsigned size);
static struct file *syscall_index_helper (int fd);
static int syscall_free_index_helper (void);
static bool syscall_chdir (const char *dir);
static bool syscall_mkdir (const char *dir);
static bool syscall_readdir (int fd, char *name);
static bool syscall_isdir (int fd);
static int syscall_inumber (int fd);

// Global lock
struct lock filesys_lock;

/* Initializes syscalls. */
void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// Carson drove here
/* Calls the corresponding system call depending on the syscode given.
   Checks the given pointers and stores the paramters using pointer arithmetic.
   Returns the results of the sytem call to the frame's eax register. */
static void
syscall_handler (struct intr_frame *f)
{
  check_pointer ((void *)(f->esp));
  int sys_code = *((int *)(f->esp));
  // Switch to the corresponding syscode defined
  switch (sys_code)
   {
     case SYS_HALT:
      {
        syscall_halt ();
        break;
      }
     case SYS_EXIT:
      {
        check_arguments (f, 1);
        int status = *((int *)(f->esp) + 1);
        syscall_exit (status);
        break;
      }
     case SYS_EXEC:
      {
        check_arguments (f, 1);
        const char *cmd_line = (const char *)(*((int *)(f->esp) + 1));
        f->eax = syscall_exec (cmd_line);
        break;
      }
     case SYS_WAIT:
      {
        check_arguments (f, 1);
        pid_t pid = *((pid_t *)(f->esp) + 1);
        f->eax = syscall_wait (pid);
        break;
      }
     case SYS_CREATE:
      {
        check_arguments (f, 2);
        const char *file = (const char *)(*((int *)(f->esp) + 1));
        unsigned initial_size = *((unsigned *)(f->esp) + 2);
        f->eax = syscall_create (file, initial_size);
        break;
      }
     case SYS_REMOVE:
      {
        check_arguments (f, 1);
        const char *file = (const char *)(*((int *)(f->esp) + 1));
        f->eax = syscall_remove (file);
        break;
      }
     case SYS_OPEN:
      {
        check_arguments (f, 1);
        const char *file = (const char *)(*((int *)(f->esp) + 1));
        f->eax = syscall_open (file);
        break;
      }
     case SYS_FILESIZE:
      {
        check_arguments (f, 1);
        int fd = *((int *)(f->esp) + 1);
        f->eax = syscall_filesize (fd);
        break;
      }
     case SYS_READ:
      {
        check_arguments (f, 3);
        int fd = *((int *)(f->esp) + 1);
        void *buffer = (void *)(*((int *)(f->esp) + 2));
        unsigned size = *((unsigned *)(f->esp) + 3);
        f->eax = syscall_read (fd, buffer, size);
        break;
      }
     case SYS_WRITE:
      {
        check_arguments (f, 3);
        int fd = *((int *)(f->esp) + 1);
        const void *buffer = (const void *)(*((int *)(f->esp) + 2));
        unsigned size = *((unsigned *)(f->esp) + 3);
        f->eax = syscall_write (fd, buffer, size);
        break;
      }
     case SYS_SEEK:
      {
        check_arguments (f, 2);
        int fd = *((int *)(f->esp) + 1);
        unsigned position = *((unsigned *)(f->esp) + 2);
        syscall_seek (fd, position);
        break;
      }
     case SYS_TELL:
      {
        check_arguments (f, 1);
        int fd = *((int *)(f->esp) + 1);
        f->eax = syscall_tell (fd);
        break;
      }
     case SYS_CLOSE:
      {
        check_arguments (f, 1);
        int fd = *((int *)(f->esp) + 1);
        syscall_close (fd);
        break;
      }
     case SYS_CHDIR:
      {
        check_arguments (f, 1);
        const char *dir = (const char *)(*((int *)(f->esp) + 1));
        f->eax = syscall_chdir (dir);
        break;
      }
     case SYS_MKDIR:
      {
        check_arguments (f, 1);
        const char *dir = (const char *)(*((int *)(f->esp) + 1));
        f->eax = syscall_mkdir (dir);
        break;
      }
     case SYS_READDIR:
      {
        check_arguments(f, 2);
        int fd = *((int *)(f->esp) + 1);
        char *name = (char *)(*((int *)(f->esp) + 2));
        f->eax = syscall_readdir (fd, name);
        break;
      }
     case SYS_ISDIR:
      {
        check_arguments (f, 1);
        int fd = *((int *)(f->esp) + 1);
        f->eax = syscall_isdir (fd);
        break;
      }
     case SYS_INUMBER:
      {
        check_arguments (f, 1);
        int fd = *((int *)(f->esp) + 1);
        f->eax = syscall_inumber (fd);
        break;
      }
     default:
      {
        // No corresponding syscall code
        syscall_exit (-1);
      }
   }
}
// End of Carson driving

// Carson drove here
/* Verifies the validity of a user-provided pointer.
   Terminate the process if the user passes a null pointer, a pointer
   to unmapped virtual memory, or a pointer to kernel virtual address space. */
static void
check_pointer (void *ptr)
{
  if (ptr == NULL || is_kernel_vaddr (ptr) ||
      pagedir_get_page (thread_current ()->pagedir, ptr) == NULL)
      syscall_exit (-1);
}
// End of Carson driving

// Carson drove here
/* Checks the validity of each argument pointer up to the 
   specified number of arguments. */
static void
check_arguments (struct intr_frame *f, int num_args)
{
  int i;
  int *ptr;
  for (i = 0; i < num_args; i++)
    {
      // Pointer arithmetic to get the next pointer
      ptr = (int *)(f->esp) + (i + 1);
      check_pointer ((void *)(ptr));
    }
}
// End of Carson driving

/* Terminates Pintos by shutting it down */
static void
syscall_halt ()
{
  shutdown_power_off ();
}

// Carson drove here
/** Terminates the current running program and returns that status
 *  to the kernel. 
 *  @param: status - exit status of current thread
 *  @return: void
*/
static void
syscall_exit (int status)
{
  // Store the relayed exit code in the current thread
  thread_current ()->exit_status = status;
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}
// End of Carson driving

// Carson drove here
/** Runs the executable file given.
 *  Parent thread waits until it knows whether the child was loaded
 *  successfuly or not.
 *  @param: cmd_line - name of executable file
 *  @return: the new process's pid or -1 if the child load failed
*/
static pid_t
syscall_exec (const char *cmd_line)
{
  check_pointer (cmd_line);

  pid_t child_pid = process_execute (cmd_line);
  // Wait until child finishes and relays its load status back
  return child_pid;
}
// End of Carson driving

/** Parent thread waits for its child process until it finishes.
 *  @param: pid - program id of the child process
 *  @return: the child's exit status or -1 if waiting failed
*/
static int
syscall_wait (pid_t pid)
{
  return process_wait (pid);
}

// Eric drove here
/** Creates a new file.
 *  @param: file - new file
 *          initial_size - initial file size in bytes
 *  @return: true if successful, false otherwise
*/
static bool
syscall_create (const char *file, unsigned initial_size)
{
  check_pointer (file);
  
  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);

  return success;
}
// End of Eric driving

// Charles drove here
/** Deletes the given file.
 *  Removes it regardless of whether it is open or closed.
 *  Removing an open file does not close it.
 *  @param: file - file to be removed
 *  @return: true if successful, false otherwise
*/
static bool
syscall_remove (const char *file)
{
  check_pointer (file);

  lock_acquire (&filesys_lock);
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);

  return success;
}
// End of Charles driving

// Charles and Eric drove here
/** Opens the given file.
 *  If successful, it adds the file if it is a valid one to a
 *  file descriptor array. Also increments the file descriptor index.
 *  @param: file - file to be opened 
 *  @return: nonnegative integer handle called a file descriptor or -1 if
 *           the file could not be opened
*/
static int
syscall_open (const char *file)
{
  check_pointer (file);
  
  struct file *fd_file;
  lock_acquire (&filesys_lock);
  
  int free_index = syscall_free_index_helper ();

  /* If there are no free indexes in the FD array, return */
  if (free_index == -1)
    return -1;

  fd_file = filesys_open (file);
  if (fd_file != NULL)
    {
      // Opens the file and update the FD array metadata
      thread_current ()->fd_table[free_index] = fd_file;
      lock_release (&filesys_lock);
      return free_index;
    }
  
  // Will return -1 if the file == NULL
  lock_release (&filesys_lock);
  return -1;
}
// End of Charles and Eric driving

// Charles drove here
// Parse for the first free index in the filesys array
static int
syscall_free_index_helper (void)
{
  int free_index = -1;
  int i;
  for (i = 2; i < 128; i++)
    {
      if (thread_current ()->fd_table[i] == NULL)
        free_index = i;
    }

  return free_index;
}
// End of Charles driving

/** Returns the size of the opened file.
 *  @param: fd - the index of the open file
 *  @return: the size, in bytes, of the open file
*/
static int
syscall_filesize (int fd)
{
  struct file *file;
  int length = -1;

  lock_acquire (&filesys_lock);
  file = syscall_index_helper (fd);
  lock_release (&filesys_lock);
  
  if (file != NULL)
    {
      lock_acquire (&filesys_lock);
      length = file_length (file);
      lock_release (&filesys_lock);
    }

  return length;
}
// End of Charles driving

// Eric drove here
/** Reads a specified number of bytes from an open file into buffer.
 *  @param: fd - the index of the open file
 *          buffer - where it will be read to
 *          size - the number of bytes to be read
 *  @return: the number of bytes actually read (0 at end of file) or -1
 *           if the file could not be read
*/
static int
syscall_read (int fd, void *buffer, unsigned size)
{
  struct file *file;
  uint8_t *buff;
  int i;

  check_pointer (buffer);

  // Reading from keyboard
  if (fd == STDIN_FILENO)
    {
      buff = buffer;
      for (i = 0; i < size; i++)
        buff[i] = (char) input_getc ();
      return size;
    }
  else
    {
      // Finds the file and returns it 
      lock_acquire (&filesys_lock);
      file = syscall_index_helper (fd);
      lock_release (&filesys_lock);
      if (file != NULL)
        {
          lock_acquire (&filesys_lock);
          // Deny write for directory
          int bytes_read = !file_getdir(file) ? 
                  file_read (file, buffer, size) : -1;
          lock_release (&filesys_lock);
          return bytes_read;
        }
    }

  return 0;
}
// End of Eric driving

// Charles drove here
/** Writes a specified number of bytes from an open file into buffer.
 *  @param: fd - the index of the open file
 *          buffer - where it will be written to
 *          size - the number of bytes to be written
 *  @return: the number of bytes actually written, which may be less than size
 *           if some bytes could not be written
*/
static int
syscall_write (int fd, const void *buffer, unsigned size)
{
  int bytes_written = 0;

  check_pointer (buffer);

  if (fd == STDIN_FILENO)
    syscall_exit (-1);

  lock_acquire (&filesys_lock);

  // Standard output
  if (fd == STDOUT_FILENO)
    {
      // Checks if size is greater than MAX_BUFFER, if so break it down
      if (size > MAX_BUFFER)
        {
          lock_release (&filesys_lock);
          return syscall_write_helper (buffer,size);
        }
      putbuf (buffer,size);
      bytes_written += size;
      lock_release (&filesys_lock);
      return bytes_written;
    }

  lock_release (&filesys_lock);
  struct file *file = syscall_index_helper (fd);
  if (file != NULL)
    {
      // Deny write for directory
      lock_acquire (&filesys_lock);
      bytes_written = !file_getdir(file) ? 
                  file_write (file, buffer, size) : -1;
      lock_release (&filesys_lock);
      return bytes_written;
    }
  bytes_written = 0;
  return bytes_written;
}
// End of Charles driving

// Charles drove here
/* Helper function that writes to the buffer with size bytes.
   Returns the number of bytes written to the buffer. */
static int
syscall_write_helper (const void *buffer, unsigned size)
{
  int bytes_written = 0;

  // Breaks large sized bytes to max amount to the buffer per call
  while (size > MAX_BUFFER)
    {
      putbuf (buffer, MAX_BUFFER);
      size -= MAX_BUFFER;
      bytes_written += MAX_BUFFER;
    }
  putbuf (buffer, size); 
  bytes_written += size;

  return bytes_written;
}
// End of Charles driving

// Eric drove here
/** Changes the positon of the next byte to be read or written in an open file.
 *  @param: fd - the index of the open file
 *          position - position where the next byte will be read/written
 *  @return: void
*/
static void
syscall_seek (int fd, unsigned position)
{
  lock_acquire (&filesys_lock);

  struct file *fd_file = syscall_index_helper (fd);
  if (fd_file != NULL)
    {
      file_seek (fd_file, position);
      lock_release (&filesys_lock);
      return;
    }

  lock_release (&filesys_lock);
  syscall_exit (-1);
}
// End of Eric driving

// Charles drove here
/** Returns the positon of the next byte to be read or written in an open file.
 *  @param: fd - the index of the open file
 *          position - position where the next byte will be read/written
 *  @return: the positon of the next byte to be read or written in an open file
*/
static unsigned
syscall_tell (int fd)
{
  lock_acquire (&filesys_lock);

  unsigned byte_offset = 0;
  struct file *fd_file = syscall_index_helper (fd);
  
  if (fd_file != NULL)
    {
      byte_offset = file_tell (fd_file);
      lock_release (&filesys_lock);
      return byte_offset;
    }
  
  lock_release (&filesys_lock);
  return byte_offset;
}
// End of Charles driving

// Charles drove here
/* Traveres through the file descriptor table and tries to find a matching fd.
   Returns a pointer to that file if it does. */
static struct file *
syscall_index_helper (int fd)
{
  int i;
  struct file *fd_file = NULL;
  for (i = 2; i < 128; i++)
    {
      if (thread_current ()->fd_table[i] != NULL && i == fd)
        fd_file = thread_current ()->fd_table[i];
    }
  return fd_file;
}
// End of Charles driving

// Charles drove here
/** Closes the given file descriptor.
 *  @param: fd - the index of the file is on
 *  @return: void
*/
static void
syscall_close (int fd)
{
  lock_acquire (&filesys_lock);
  
  struct file *fd_close = syscall_index_helper (fd);
  if (fd_close != NULL)
    {
      file_close (fd_close);
      thread_current ()->fd_table[fd] = NULL;
    }

  lock_release (&filesys_lock);
}
// End of Charles driving

// Carson drove here
/** Changes the current working directory of the process to dir,
 *  which may be relative or absolute.
 *  @param: dir - the current working directory
 *  @return: true if successful, false on failure
*/
static bool
syscall_chdir (const char *dir)
{
  bool success;
  lock_acquire (&filesys_lock);
  success = dir != NULL ? dir_chdir (dir) : false;
  lock_release (&filesys_lock);
  return success;
}
// End of Carson driving

// Eric drove here
/** Makes a new directory and will return true if it was successful
 *  or false if it didn't make it. Also fails if directory already exist.
 *  @param: dir - dir, it might already exist
 *  @return: true if successful, false on failure
*/
static bool
syscall_mkdir (const char *dir)
{
  bool success;
  lock_acquire (&filesys_lock);
  success = dir != NULL ? dir_mkdir (dir) : false;
  lock_release (&filesys_lock);
  return success;
}
// End of Eric driving

// Charles drove here
/** Reads a directory entry from fd. If it is valid it will store the
 *  the null terminated file in name. If there is no more entries in the
 *  directory it will return false.
 *  @param: fd - file descriptor
 *          name - file name
 *  @return: true if successful, false otherwise
*/
static bool
syscall_readdir (int fd, char *name)
{
  bool retVal = false;
  lock_acquire (&filesys_lock);
  // If the file descriptor array exists, attempt to retrieve
  if (thread_current ()->fd_table[fd] != NULL)  
    retVal = 
      dir_readdir (file_getdir ((thread_current ()->fd_table[fd])), name);
  lock_release (&filesys_lock);
  return retVal;
}
// End of Charles driving

// Eric and Carson drove here
/** Determines whether or not the open file in the 
 *  file descriptor array is a directory.
 *  @param: fd - file descriptor
 *  @return: true if directory, false if ordinary file
*/
static bool
syscall_isdir (int fd)
{
  bool retVal = false;
  lock_acquire (&filesys_lock);
  // If the file descriptor array exists, attempt to retrieve
  if (thread_current ()->fd_table[fd] != NULL)
    retVal = file_get_inode (thread_current()->fd_table[fd])->data.is_dir;
  lock_release (&filesys_lock);
  return retVal;
}
// End of Eric and Carson driving

// Sumedh drove here
/** Returns the inumber of the open file descriptor
 *  @param: fd - file descriptor
 *  @return: -1 on error, otherwise the inumber of the open file
*/
static int
syscall_inumber (int fd)
{
  int inumber = -1;
  lock_acquire (&filesys_lock);
  if (thread_current ()->fd_table[fd] != NULL)
    inumber = 
      inode_get_inumber (file_get_inode (thread_current ()->fd_table[fd]));
  lock_release (&filesys_lock);
  return inumber;
}
// End of Sumedh driving
