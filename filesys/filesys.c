#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"

// Partition that contains the file system
struct block *fs_device;
// Global dir lock
struct lock dir_lock;
// Global parsed path size
int path_size;

// Method declarations
static void do_format (void);
static int pre_parse_setup (const char *name, int name_len, char *delimiter);
static char **tokens_array (char **tokens, const char *name,
                            char *temp_path, char *delimiter);

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
  lock_init (&dir_lock);

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
}

// Sumedh drove here
/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  path_size = 0;
  char **paths = get_paths (name);
  bool not_null;
  bool success = false;
  block_sector_t inode_sector = 0;


  if (paths != NULL)
    {
      struct dir *curr = find_path (paths);

      // lock so we can create a new file
      lock_acquire (&dir_lock);
      not_null = curr != NULL;
      success =  not_null && free_map_allocate (1, &inode_sector)
                          && inode_create (inode_sector, initial_size, false)
                          && dir_add (curr, get_file_name(paths), inode_sector);

      if (!(success || !(inode_sector != 0)))
        free_map_release (&inode_sector, 1);
      
      lock_release (&dir_lock); 

      dir_close (curr);
      path_dealloc (paths);
    }
  return success;
}
// End of Sumedh driving

// Eric drove here
/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  path_size = 0;
  bool res = false;
  char **paths = get_paths (name);

  if (path_size <= 0)
    return false;

  if (paths != NULL)
    {
      char *to_remove = get_file_name (paths);

      struct dir *curr = find_path (paths);

      // make sure they all run together or not
      lock_acquire (&dir_lock);
      res = curr != NULL && dir_remove (curr, to_remove);
      lock_release (&dir_lock);

      dir_close (curr);

      path_dealloc (paths);
    }

  return res;
}
// End of Eric driving

// Charles drove here
/**
 * Returns the new file if successful or a null pointer
 * otherwise.
 * 
 * Returns the new file if successful or a null pointer otherwise.
 */
struct file *
filesys_open (const char *name)
{
  path_size = 0;
  char **paths = get_paths (name);
  struct inode *inode = NULL;
  struct file *res = NULL;

  if (!paths)
    return NULL;
  else if (path_size == -1)
    return NULL;

  bool non_root = false;
  char *file_name = get_file_name (paths);

  bool root_case = !strcmp (file_name, "/") && path_size == 1;
  // Edge case for opening root
  if (root_case)
    {
      path_dealloc (paths);
      res = file_open (inode_open (ROOT_DIR_SECTOR));
      non_root = true;
    }

  // Lookup proccess
  if (!non_root)
    {
      struct dir *curr = find_path (paths);
      if (curr)
        {
          lock_acquire (&dir_lock);
          dir_lookup (curr, file_name, &inode);
          lock_release (&dir_lock);
        }
      dir_close (curr);
      path_dealloc(paths);
      res = file_open (inode);
    }

  return res;
}
// End of Charles driving

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

// Carson drove here
/**
 * Returns the number of tokens in the path.
 */
static int
pre_parse_setup (const char *name, int name_len, char *delimiter)
{
  if (name_len < 1)
    return -1;
  int num_tokens = 0;
  
  // Check if its the root and add one if so
  if (name[0] == '/')
    num_tokens++;

  // Create temp path since it will be destroyed
  char *temp_path = (char *) malloc (sizeof (char) * (name_len + 1));
  if (temp_path == NULL)
    return NULL;
  strlcpy (temp_path, name, name_len + 1);

  char *s_ptr;
  char *token = strtok_r (temp_path, delimiter, &s_ptr);

  // Get the size of paths
  while (token)
    {
      token = strtok_r (NULL, delimiter, &s_ptr);
      num_tokens++;
    }
  free (temp_path);
  
  return num_tokens;
}
// End of Carson driving

// Sumedh drove here
/**
 * Returns a a char array of chars holding the tokens of the path.
 */
static char **
tokens_array (char **tokens, const char *name, char *temp_path, char *delimiter)
{
  int index = 0;
  if (tokens == NULL)
    return NULL;

  // Fill the array
  if (name[0] == '/')
    {
      char *r = malloc (2 * sizeof (char));

      r[0] = '/';
      r[1] = '\0';
      tokens[index] = r;
      index++;
    }

  char *token = NULL;
  char *s_ptr = NULL;
  token = strtok_r (temp_path, delimiter, &s_ptr);

  // Free the tokens/paths
  while (token)
    {
      char *curr_path = malloc ((strlen (token) + 1) * sizeof (char));
      if (curr_path == NULL)
        {
          int i = 0;
          // Free elements then the whole
          while (i < index) {
            free (tokens[index]);
            i++;
          }
          free (tokens);
          return NULL;
        }
      strlcpy (curr_path, token, strlen (token) + 1);
      tokens[index] = curr_path;
      index++;
      token = strtok_r (NULL, delimiter, &s_ptr);
    }
    return tokens;
}
// End of Sumedh driving

// Carson drove here
/**
 * Gets the tokens array with the paths.
 */
char **
get_paths (const char *name)
{
  int name_len = strlen (name);
  int num_tokens = 0;
  char *delimiter = "/\n";

  num_tokens = pre_parse_setup (name, name_len, delimiter);

  if (num_tokens == -1) {
    return NULL;
  }  

  // Temp paths to be created to be destroyed
  char *temp_path = (char *) malloc (sizeof (char) * (name_len + 1));
  if (temp_path == NULL)
    return NULL;
  strlcpy (temp_path, name, name_len + 1);

  // Allocate tokens array to return
  char **tokens = (char **) malloc (sizeof (char *) * (num_tokens + 1));
  if (tokens == NULL)
    {
      free (temp_path);
      return NULL;
    }
  
  tokens = tokens_array (tokens, name, temp_path, delimiter);
  free (temp_path);
  path_size = num_tokens;
  return tokens;
}
// End of Carson driving

// Eric drove here
/**
 * Frees the paths in tokens array.
 */
void
path_dealloc (char **paths)
{
  int i = 0;
  while (i < path_size)
    {
      free (paths[i]);
      i++;
    }
  free (paths);
}
// End of Eric driving

// Everyone drove here
/**
 * Traverses the tokens array to find dir path.
 */
struct dir *
find_path (char **paths)
{  
  struct dir *curr;
  bool root = true;

  if (strcmp (paths[0], "/"))
    root = false;
  
  // Case if its the root
  if (root)
    curr = dir_open_root();
  else if (!thread_current()->i_dir)
    curr = dir_open_root();
  else
    curr = dir_open (inode_reopen (thread_current ()->i_dir));
  
  int i = (int) root;
  // Check all elements in the paths
  while (i < path_size - 1)
    {
      lock_acquire (&dir_lock);

      char *curr_path = paths[i];
      struct inode *inode = NULL;

      // Check valid lookup
      bool bad_lookup = !dir_lookup (curr, curr_path, &inode);

      bool invalid = 0;
      // If it's a bad lookup/not valid dir then close accordingly
      if (bad_lookup || !inode->data.is_dir)
        {
          if (inode != NULL)
            inode_close (inode);
          invalid = true;
        }
      if (invalid)
        {
          lock_release (&dir_lock); 
          dir_close (curr);
          return NULL;
        }
      else
        {
          struct dir *next = dir_open (inode);
          lock_release (&dir_lock);
          dir_close (curr);
          curr = next;
        }

      i++;
    }

  return curr;
}
// End of everyone driving

// Carson drove here
/* Returns the name of file. */
char *
get_file_name (char **tokens)
{
  return tokens[path_size - 1];
}
// End of Carson driving
