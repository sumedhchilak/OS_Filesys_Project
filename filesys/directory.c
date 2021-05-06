#include <stdio.h>
#include <string.h>
#include <list.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/free-map.h"

char *DOT = ".";
char *DBL_DOT = "..";

extern int path_size;
extern struct lock dir_lock;

/* Helper method */
static bool is_dir_empty (struct dir *dir);

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt, block_sector_t parent)
{
  off_t length = entry_cnt * sizeof (struct dir_entry);
  bool retVal = false;

  // if there is a valid inode on disk
  if (inode_create (sector, length, true))
    {
      bool p = false;
      bool s = false;

      // attempt to open a directory
      struct dir *dir = dir_open (inode_open (sector));
      if (!dir)
        return false;

      s = dir_add (dir, DOT, sector);
      p = dir_add (dir, DBL_DOT, parent);

      // append dot and dbl dot to the directory and return
      dir_close (dir);
      
      retVal = s && p;
    }
  return retVal;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    {
      if (e.in_use && !strcmp (name, e.name)) 
        {
          if (ep != NULL)
            *ep = e;
          if (ofsp != NULL)
            *ofsp = ofs;
          return true;
        }
    }

  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

  done:
    return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

   /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;
  
  /* If the directory entry open, dont remove */
  if (inode->data.is_dir && inode->open_cnt > 1)
    goto done;

  /* Find directory entry that is not empty and not open, dont remove */
  if (inode->data.is_dir)
    {
      struct inode *curr = inode_reopen (inode);
      struct dir *other_dir = dir_open (curr);
      bool empty = is_dir_empty (other_dir);

      if (!empty)
        goto done;
    }

  /* Remove inode. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;

      /* Parse through the dot/dbl dots assigned to each directory entry */
      bool success1 = strcmp (e.name, DOT);
      bool success2 = strcmp (e.name, DBL_DOT);

      if (e.in_use && success1 && success2)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }

  return false;
}

// Eric and Sumedh drove here
/* Checks whether dir is empty and returns true if so. */
static bool
is_dir_empty (struct dir *dir)
{
  if (dir)
    {
      struct dir_entry cur;
      off_t index;

      for (index = sizeof (cur); inode_read_at (dir->inode, &cur, 
              sizeof (cur), index) == sizeof (cur); index += sizeof (cur))
        {
          /* Parse through the dot and dbl dots assigned to each 
            directory entry */
          bool success1 = strcmp (cur.name, DOT);
          bool success2 = strcmp (cur.name, DBL_DOT);
          if (cur.in_use && success1 && success2)
            {
              dir_close (dir);
              return false;
            }
        }
    }
  else
    return false;

  // if the directory is completely empty
  dir_close (dir);
  return true;
}
// End of Eric and Sumedh driving

// Eric and Sumedh drove here
/* Creates a new directory if possible, returns true if successful and false
   if it failed. Fails mean dir already exist or a dir is also named that */
bool
dir_mkdir (const char *name)
{
  path_size = 0;
  bool success = false;

  char **paths = get_paths (name);

  if (!paths)
    return success;
  
  // available path_size
  if (path_size >= 0)
    {
      char *file_name = get_file_name (paths);

      block_sector_t sec = 0;

      struct dir *curr = find_path (paths);

      lock_acquire (&dir_lock);

      // pre-reqs checks to see if its possible to create
      bool not_null = curr != NULL && free_map_allocate (1, &sec);
      bool check_dir = false;

      // See if we can add a new directory
      if (not_null)
        {
          block_sector_t inode_num = inode_get_inumber (dir_get_inode (curr));
          check_dir = dir_create (sec, 2, inode_num);
        }
      
      success = check_dir && dir_add (curr, file_name, sec);

      // free if neccesary
      if (!(success || !(sec != 0)))
        free_map_release (&sec, 1);
      
      lock_release (&dir_lock);

      dir_close (curr);
      path_dealloc (paths);

      return success;
    }
  
  return false;
}
// End of Eric and Sumedh driving

// Eric and Sumedh drove here
/* Switches the current working directory of the given name(dir).
   Returns true if successful, false if it failed. */
bool
dir_chdir (const char *name)
{
  path_size = 0;
  char **paths = get_paths (name);

  if (!paths)
    return false;
  else if (path_size == -1)
    return -1;

  char *file_name = get_file_name (paths);
  
  bool root_case = !strcmp (file_name, "/") && path_size == 1;
  // Edge case for root
  if (root_case)
    {
      path_dealloc (paths);
      struct inode *inode = thread_current ()->i_dir;
      thread_current ()->i_dir = NULL;
      inode_close (inode);
      return true;
    }

  struct dir *curr = find_path (paths);
  struct inode* inode = NULL;

  lock_acquire (&dir_lock);
  
  bool valid = dir_lookup (curr, file_name, &inode);

  // See if its a valid lookup/inode and close accordingly
  if (!inode || !valid)
    {
      path_dealloc (paths);
      dir_close (curr);
      lock_release (&dir_lock);
      return false;
    }

  struct inode *close = thread_current ()->i_dir;
  thread_current ()->i_dir = inode;

  lock_release (&dir_lock);

  inode_close (close);
  dir_close (curr);
  path_dealloc (paths);

  return valid;
}
// End of Eric and Sumedh driving
