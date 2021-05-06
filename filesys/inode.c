#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/inode.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"

// Free map bitmap
extern struct bitmap *free_map;

// Method declarations
static block_sector_t ptrs_direct (block_sector_t index, 
                                   const struct inode *inode);
static block_sector_t ptrs_indirect (block_sector_t index, 
                                     const struct inode *inode);
static block_sector_t ptrs_doubly_indirect (block_sector_t index, 
                                            const struct inode *inode);
static bool valid_allocation (struct inode_disk *disk_inode,
                              block_sector_t sectors);
static size_t valid_dbl_allocation (struct inode_disk *disk_inode, 
                          size_t sectors, char empty_arr[BLOCK_SECTOR_SIZE]);
static size_t write_back (size_t sectors, char empty_arr[BLOCK_SECTOR_SIZE], 
                          block_sector_t indirect[IND_BLK_PTR_CNT]);
static bool inode_growth (struct inode_disk *disk_inode,
                          block_sector_t sector, off_t size, off_t offset);
static bool inode_alloc (size_t count, block_sector_t *sector);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

// Carson drove here
/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  // Error on invalid inode length
  if (pos >= inode->data.length)
    return -1;
  else
    {
      block_sector_t index = pos / BLOCK_SECTOR_SIZE;
    
      while (1)
        {
          block_sector_t val_dir = ptrs_direct (index, inode);
          if (val_dir != NULL)
            return val_dir;

          index -= DIRECT_CNT;
          block_sector_t val_indir = ptrs_indirect (index, inode);
          if (val_indir != NULL)
            return val_indir;

          index -= IND_BLK_PTR_CNT;
          block_sector_t val_dbl_indir = ptrs_doubly_indirect (index, inode);
          if (val_dbl_indir != NULL)
            return val_dbl_indir;

          break;
        }
    }
}
// End of Carson driving

// Carson drove here
/* For direct pointers, returns the block sector of a indexed data block
   for an inode. */
static block_sector_t
ptrs_direct (block_sector_t index, const struct inode *inode)
{
  if (index < DIRECT_CNT)
    return inode->data.blocks[index];
  else
    return NULL;
}
// End of Carson driving

// Sumedh drove here
/* For indirect pointers, returns the block sector of a indexed data block
   for an inode. */
static block_sector_t
ptrs_indirect (block_sector_t index, const struct inode *inode)
{
  if (index < IND_BLK_PTR_CNT)
    {
      block_sector_t ptrs[IND_BLK_PTR_CNT];
      block_read (fs_device, inode->data.blocks[INDIRECT_INDEX], ptrs);
      return ptrs[index];  
    }
  else
    return NULL;
}
// End of Sumedh driving

// Eric and Sumedh drove here
/* For doubly indirect pointers, returns the block sector of a 
  indexed data blockfor an inode. */
static block_sector_t 
ptrs_doubly_indirect (block_sector_t index, const struct inode *inode)
{
  if (index < DBL_IND_BLK_PTR_CNT)
    {
      // Doubly indirect block
      block_sector_t doubly_indirect_ptrs[IND_BLK_PTR_CNT];
      block_read (fs_device, inode->data.blocks[DOUBLY_INDIRECT_INDEX],
                          doubly_indirect_ptrs);
      // Indirect block
      block_sector_t indir_index = (index) / IND_BLK_PTR_CNT;
      block_sector_t indirect_ptrs[IND_BLK_PTR_CNT];
      block_read (fs_device, doubly_indirect_ptrs[indir_index], indirect_ptrs);
      // Return block sector using a factor of indirect pointers for the index
      return indirect_ptrs[index % IND_BLK_PTR_CNT];
    }
  else
    return NULL;
}
// End of Eric and Sumedh driving

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

// Sumedh drove here
/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool retVal = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  // Struct metadata
  disk_inode = calloc (1, sizeof *disk_inode);
  disk_inode->is_dir = is_dir;
  disk_inode->length = 0;

  retVal = inode_growth (disk_inode, sector, length, 0);
  free (disk_inode);
  return retVal;
}
// End of Sumedh driving

// Eric drove here
/* Allocates inodes based on direct, indirect, and/or doubly indirect blocks. */
static bool
valid_allocation (struct inode_disk *disk_inode, block_sector_t sectors)
{
  // Direct block allocation
  if (sectors <= DIRECT_CNT) 
    return inode_alloc (sectors, disk_inode->blocks);
  // Direct and single indirect block allocation
  if (sectors <= IND_BLK_PTR_CNT + DIRECT_CNT)
    return inode_alloc (DIRECT_CNT + INDIRECT_CNT, disk_inode->blocks);
  // Direct, indirect, and doubly indirect block allocation
  if (sectors <= DBL_IND_BLK_PTR_CNT + DIRECT_CNT)
    return inode_alloc (DIRECT_CNT + INDIRECT_CNT + DOUBLY_INDIRECT_CNT,
                      disk_inode->blocks);
  return false;
}
// End of Eric driving

// Sumedh and Eric drove here
/* Returns the number of indirect block pointers for the sector write. */
static size_t
write_back (size_t sectors, char empty_arr[BLOCK_SECTOR_SIZE], 
              block_sector_t indirect_arr[IND_BLK_PTR_CNT])
{
  size_t ind_blk_ptrs;
  if (sectors <= IND_BLK_PTR_CNT)
    ind_blk_ptrs = sectors;
  else
    ind_blk_ptrs = IND_BLK_PTR_CNT;
  inode_alloc (ind_blk_ptrs, indirect_arr);

  int i = 0;
  while (i < ind_blk_ptrs)
    {
      if (indirect_arr[i] == NULL)
        block_write (fs_device, indirect_arr[i], empty_arr);
      i++;
    }
  return ind_blk_ptrs;
}
// End of Sumedh and Eric driving

// Sumedh and Eric drove here
/* Returns the number of sectors for 
   the indirect block pointer allocation. */
static size_t
valid_ind_allocation (struct inode_disk *disk_inode, size_t sectors, 
              char empty_arr[BLOCK_SECTOR_SIZE])
{
  block_sector_t indirect[IND_BLK_PTR_CNT];
  if (disk_inode->blocks[INDIRECT_INDEX]) 
    block_read (fs_device, disk_inode->blocks[INDIRECT_INDEX], indirect);
  
  size_t ind_blk_ptrs = write_back (sectors, empty_arr, indirect);
  block_write (fs_device, disk_inode->blocks[INDIRECT_INDEX], indirect);
  sectors -= ind_blk_ptrs;
  return sectors;
}
// End of Sumedh and Eric driving

// Sumedh and Eric drove here
/* Returns the number of sectors for 
   the doubly indirect block pointer allocation. */
static size_t
valid_dbl_allocation (struct inode_disk *disk_inode, 
                      size_t sectors, char empty_arr[BLOCK_SECTOR_SIZE]) 
{
  // Read data blocks into doubly indirect array
  block_sector_t doubly_indirect[IND_BLK_PTR_CNT];
  if (disk_inode->blocks[DOUBLY_INDIRECT_INDEX]) 
    block_read (fs_device, disk_inode->blocks[DOUBLY_INDIRECT_INDEX],
                     doubly_indirect);

  // Number of doubly indirect pointers necessary for first-level indirection
  size_t num_dbly_indir_ptrs = (sectors - 1) / IND_BLK_PTR_CNT + 1;
  inode_alloc (num_dbly_indir_ptrs, doubly_indirect);

  int i = 0;
  while (i < num_dbly_indir_ptrs) 
    {
      // Read data blocks into the indirect array
      block_sector_t indirect_arr[IND_BLK_PTR_CNT];
      if (doubly_indirect[i] != NULL)
        block_read (fs_device, doubly_indirect[i], indirect_arr);

      size_t ind_blk_ptrs = write_back (sectors, empty_arr, indirect_arr);
      block_write (fs_device, doubly_indirect[i], indirect_arr);
      sectors -= ind_blk_ptrs;
      i++;
    }
  block_write (fs_device, disk_inode->blocks[DOUBLY_INDIRECT_INDEX],
               doubly_indirect);
  return sectors;
}
// End of Sumedh and Eric driving

// Everyone drove here
bool 
inode_growth (struct inode_disk *disk_inode, block_sector_t sector,
              off_t size, off_t offset)
{
  bool success = false;
  if (disk_inode != NULL)
    {
      // Empty array with the size of a block sector
      char empty_arr[BLOCK_SECTOR_SIZE];

      // Set metadata
      size_t sectors = bytes_to_sectors (offset + size);
      disk_inode->magic = INODE_MAGIC;

      // Check if allocation was valid
      success = valid_allocation (disk_inode, sectors);
      if (!success)
        return false;
        
      // Update inode length and write to disk
      disk_inode->length = offset + size;
      block_write (fs_device, sector, disk_inode);
      
      size_t dir_blk_ptrs = NULL;
      if (sectors <= DIRECT_CNT)
        dir_blk_ptrs = sectors;
      else
        dir_blk_ptrs = DIRECT_CNT;
      
      // Write the indirect block pointers to disk
      int i = 0;
      while (i < dir_blk_ptrs)
        {
          // Check if the direct block has been written
          bool valid = disk_inode->blocks[i];
          if (!valid)
            block_write (fs_device, disk_inode->blocks[i], empty_arr);
          i++;
        }

      // If needed, write the indirect blocks
      sectors -= dir_blk_ptrs;
      if (sectors > 0)
        sectors = valid_ind_allocation(disk_inode, sectors, empty_arr);
      
      // If needed, write the doubly indirect blocks
      if (sectors > 0)
        sectors = valid_dbl_allocation (disk_inode, sectors, empty_arr);
    }
  return success;
}
// End of everyone driving

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
  lock_init (&inode->inode_lock);
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
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

/* Closes INODE and writes it to disk. (Does it?  Check code.)
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
          // Everyone drove here
          block_sector_t ind_blk_ptrs[IND_BLK_PTR_CNT];
          block_sector_t indirect = inode->data.blocks[INDIRECT_INDEX];
          block_sector_t doubly_indirect = 
              inode->data.blocks[DOUBLY_INDIRECT_INDEX];

          // Block deallocation
          int i = 0;
          while (i < DIRECT_CNT)
            {
              if (inode->data.blocks[i])
                free_map_release (inode->data.blocks[i], 1);
              i++;
            }

          // Indirect block freeing
          if (indirect)
            {
              block_read (fs_device, indirect, ind_blk_ptrs);
              // Free all indiect block pointers
              int i = 0;
              while (i < IND_BLK_PTR_CNT)
                {
                  if (ind_blk_ptrs[i])
                    free_map_release (ind_blk_ptrs[i], 1);
                  i++;
                }
              // Free indirect block
              free_map_release (indirect, 1);
            }
          
          // Doubly indirect block freeing
          if (doubly_indirect)
            {
              block_read (fs_device, doubly_indirect, ind_blk_ptrs);

              // Indirect block freeing
              int i = 0;
              while (i < IND_BLK_PTR_CNT)
                {
                  if (ind_blk_ptrs[i])
                    {
                      block_read (fs_device, ind_blk_ptrs[i], ind_blk_ptrs);
                      // Free all indirect block pointers
                      int j = 0;
                      while (j < IND_BLK_PTR_CNT)
                        {
                          if (ind_blk_ptrs[j])
                            free_map_release (ind_blk_ptrs[j], 1);
                          j++;
                        }
                      // Free indirect block
                      free_map_release (ind_blk_ptrs[i], 1);
                    }
                  i++;
                }
              // Free doubly indirect block
              free_map_release (doubly_indirect, 1);
            }
          
          free_map_release (inode->sector, 1);
        }
      else
        block_write (fs_device, inode->sector, &inode->data);
      // End of everyone driving

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
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->data.length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

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
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  // Charles drove here
  // Check if inode growth is needed
  bool need_growth = (size + offset) > inode->data.length;
  if (need_growth)
    {
      lock_acquire (&inode->inode_lock);

      bool success = inode_growth (&inode->data, inode->sector, size, offset);
      if (!success)
        {
          lock_release (&inode->inode_lock);
          return 0;
        }
      lock_release (&inode->inode_lock);
    }
  // End of Charles driving

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->data.length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written); //^_^
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all empty_arr. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

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
 
// Sumedh and Eric drove here
/* Allocate blocks for inode */
bool
inode_alloc (size_t blk_count, block_sector_t *sector) 
{
  bool dump = true;
  int i = 0;
  // Allocate until the block count is reached or bitmap is dumped
  while (i < blk_count && dump)
    {
      if (sector[i])
        {
          i++;
          continue;
        }
      if (!free_map_allocate (1, sector + i))
        {
          bitmap_dump (free_map);
          dump = false;
        }
      i++;
    }
  return dump;
}
// End of Eric and Sumedh driving
