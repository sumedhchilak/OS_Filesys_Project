#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

// Number of direct, indirect, and doubly indirect pointers
#define DIRECT_CNT 10
#define INDIRECT_CNT 1
#define DOUBLY_INDIRECT_CNT 1

// The indicies for indirect and doubly indirect pointers
#define INDIRECT_INDEX 10
#define DOUBLY_INDIRECT_INDEX 11

// Number of pointers in an indirect or doubly indirect block
#define IND_BLK_PTR_CNT \
    (block_sector_t) (BLOCK_SECTOR_SIZE / sizeof (block_sector_t))
#define DBL_IND_BLK_PTR_CNT IND_BLK_PTR_CNT * IND_BLK_PTR_CNT

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    // All direct, indirect, and doubly indirect blocks together.
    block_sector_t blocks[DIRECT_CNT + INDIRECT_CNT + DOUBLY_INDIRECT_CNT];
    bool is_dir;                        /* Indicates if a directory. */
    uint32_t unused[113];               /* Not used. */
  };

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock inode_lock;             /* Inode lock. */
  };

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);

#endif /* filesys/inode.h */
