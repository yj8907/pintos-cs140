#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define INODE_META_SIZE 8
#define NUM_DIRECT 16
#define NUM_INDIRECT 32
#define NUM_DOUBLE_INDIRECT 4

#define ENTRY_SIZE 4
#define NUM_ENTRY_INDIRECT_SINGLE (BLOCK_SECTOR_SIZE/ENTRY_SIZE)
#define NUM_ENTRY_INDIRECT_DOUBLE (NUM_ENTRY_INDIRECT_SINGLE * NUM_ENTRY_INDIRECT_SINGLE)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
//    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t direct_blocks[NUM_DIRECT];
    uint32_t indirect_single_blocks[NUM_INDIRECT];
    uint32_t indirect_double_blocks[NUM_DOUBLE_INDIRECT];
    uint32_t unused[74];               /* Not used. */
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
//    struct inode_disk *data;             /* Inode content. */
    struct lock inode_lock;
  };

static void
inode_read_index(block_sector_t block, size_t offset, uint32_t *sector, bool allocate)
{
    void *cache = cache_allocate_sector(block, CACHE_READ);
    cache_read(cache, sector, offset, ENTRY_SIZE);
    
    if (*sector == 0 && allocate) {
        ASSERT(free_map_allocate (1, sector));
        
        cache = cache_allocate_sector(block, CACHE_WRITE);
        block_sector_t sector_read = cache_index_write(cache, sector, offset);
        
        if (sector_read == *sector) {
            static char zeros[BLOCK_SECTOR_SIZE];
            void *inode_cache = cache_allocate_sector(*sector, CACHE_WRITE);
            cache_write(inode_cache, &zeros, 0, BLOCK_SECTOR_SIZE);
        } else {
            free_map_release(*sector, 1);
            *sector = sector_read;
        }
    }
    
    if (allocate) ASSERT(*sector != 0);
    
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Allocate new block if the position has not been assigned a block.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
/* dynamic allocate new sector */
static block_sector_t
byte_to_sector(const struct inode *inode, off_t pos, bool allocate){
    ASSERT (inode != NULL);
          
    uint32_t index_pos;
    uint32_t *sector;
    uint32_t offset;
    uint32_t index_sector;
    
    if (pos < NUM_DIRECT * BLOCK_SECTOR_SIZE) {
        index_pos = pos / BLOCK_SECTOR_SIZE;
        offset = INODE_META_SIZE+index_pos*ENTRY_SIZE;
        index_sector = inode->sector;
        inode_read_index(index_sector, offset, sector, allocate);
        
        if (*sector == 0) return -1;
        return *sector;
        
    } else if ( (pos -= NUM_DIRECT * BLOCK_SECTOR_SIZE) <
               NUM_INDIRECT * NUM_ENTRY_INDIRECT_SINGLE * BLOCK_SECTOR_SIZE) {
        
        index_pos = pos / (NUM_ENTRY_INDIRECT_SINGLE * BLOCK_SECTOR_SIZE);
        offset = INODE_META_SIZE+(NUM_DIRECT+index_pos)*ENTRY_SIZE;
        index_sector = inode->sector;
        inode_read_index(index_sector, offset, sector, allocate);
        if (*sector == 0 ) return -1;
        
        index_sector = *sector;
        pos -= index_pos * NUM_ENTRY_INDIRECT_SINGLE * BLOCK_SECTOR_SIZE;
        index_pos = pos/BLOCK_SECTOR_SIZE;
        offset = index_pos*ENTRY_SIZE;
        inode_read_index(index_sector, offset, sector, allocate);
        if ( *sector == 0 ) return -1;
        
        return *sector;
        
    } else if ( (pos -= NUM_INDIRECT * NUM_ENTRY_INDIRECT_SINGLE * BLOCK_SECTOR_SIZE)
               < NUM_DOUBLE_INDIRECT * NUM_ENTRY_INDIRECT_DOUBLE * BLOCK_SECTOR_SIZE) {
        
        index_pos = pos / (NUM_ENTRY_INDIRECT_DOUBLE * BLOCK_SECTOR_SIZE);
        offset = INODE_META_SIZE+(NUM_DIRECT+NUM_INDIRECT+index_pos)*ENTRY_SIZE;
        index_sector = inode->sector;
        inode_read_index(index_sector, offset, sector, allocate);
        if (*sector == 0 ) return -1;
        
        index_sector = *sector;
        pos -= index_pos * NUM_ENTRY_INDIRECT_DOUBLE * BLOCK_SECTOR_SIZE;
        index_pos = pos/(NUM_ENTRY_INDIRECT_SINGLE * BLOCK_SECTOR_SIZE);
        offset = index_pos*ENTRY_SIZE;
        inode_read_index(index_sector, offset, sector, allocate);
        if ( *sector == 0 ) return -1;
        
        index_sector = *sector;
        pos -= index_pos * NUM_ENTRY_INDIRECT_SINGLE * BLOCK_SECTOR_SIZE;
        index_pos = pos/ BLOCK_SECTOR_SIZE;
        offset = index_pos*ENTRY_SIZE;
        inode_read_index(index_sector, offset, sector, allocate);
        if ( *sector == 0 ) return -1;
        
        return *sector;
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
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  ASSERT ((void*)disk_inode->direct_blocks - (void*)disk_inode == INODE_META_SIZE);
    
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      block_write (fs_device, sector, disk_inode);
      free (disk_inode);
      success = true;
    }
  return success;
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
//  block_read (fs_device, inode->sector, &inode->data);
  lock_init(&inode->inode_lock);
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

/* free sparsely allocated blocks */
static void
inode_free_map_release(struct inode *inode)
{
    void *cache;
    uint32_t *sector;
    void *buffer = malloc(BLOCK_SECTOR_SIZE);
    void *buffer2 = malloc(BLOCK_SECTOR_SIZE);
    void *buffer3 = malloc(BLOCK_SECTOR_SIZE);
    
    cache = cache_allocate_sector(inode->sector, CACHE_READ);
    cache_read(cache, buffer, 0, BLOCK_SECTOR_SIZE);
    
    off_t inode_length = *(off_t*)buffer;
    off_t bytes_scanned = 0;
    
    if (bytes_scanned < inode_length) {
        for (size_t i = 0; i < NUM_DIRECT; i++){
            sector = (uint32_t*)(buffer + INODE_META_SIZE + i*ENTRY_SIZE);
            if (*sector != 0) free_map_release (*sector, 1);
            if ((bytes_scanned+=BLOCK_SECTOR_SIZE) >= inode_length) goto done;
        }
    }
    
    if (bytes_scanned < inode_length) {
        for (size_t i = 0; i < NUM_INDIRECT; i++){
            sector = (uint32_t*)(buffer + INODE_META_SIZE + NUM_DIRECT*ENTRY_SIZE + i*ENTRY_SIZE);
            if (*sector != 0) {
                cache = cache_allocate_sector(*sector, CACHE_READ);
                cache_read(cache, buffer2, 0, BLOCK_SECTOR_SIZE);
                for (size_t j = 0; j < BLOCK_SECTOR_SIZE/ENTRY_SIZE; ++j) {
                    sector = (uint32_t*)(buffer2 + j * ENTRY_SIZE);
                    if (*sector != 0) free_map_release (*sector, 1);
                    if ((bytes_scanned+=BLOCK_SECTOR_SIZE) > inode_length) goto done;
                }
            }
        }
    }
    
    if (bytes_scanned < inode_length) {
        for (size_t i = 0; i < NUM_DOUBLE_INDIRECT; i++){
            sector = (uint32_t*)(buffer + INODE_META_SIZE + (NUM_DIRECT+NUM_INDIRECT)*ENTRY_SIZE + i*ENTRY_SIZE);
            if (*sector != 0) {
                cache = cache_allocate_sector(*sector, CACHE_READ);
                cache_read(cache, buffer2, 0, BLOCK_SECTOR_SIZE);
                for (size_t j = 0; j < BLOCK_SECTOR_SIZE/ENTRY_SIZE; ++j) {
                    sector = (uint32_t)(buffer2 + j * ENTRY_SIZE);
                    if (*sector != 0) {
                        cache = cache_allocate_sector(*sector, CACHE_READ);
                        cache_read(cache, buffer3, 0, BLOCK_SECTOR_SIZE);
                        for (size_t k = 0; k < BLOCK_SECTOR_SIZE/ENTRY_SIZE; ++k) {
                            sector = (uint32_t*)(buffer3 + k * ENTRY_SIZE);
                            if (*sector != 0) free_map_release (*sector, 1);
                            if ((bytes_scanned+=BLOCK_SECTOR_SIZE) > inode_length) goto done;
                        }
                    }
                }
            }
        }
    };
    
  done:
    free(buffer);
    free(buffer2);
    free(buffer3);
    
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
          inode_free_map_release(inode);
          free_map_release (inode->sector, 1);
//          free_map_release (inode->data.start,
//                            bytes_to_sectors (inode->data.length));
        }

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
  uint8_t *cache = NULL;
//  uint8_t *bounce = NULL;
    PANIC("read_test");
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, false);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
        
      if (sector_idx != -1) {
          cache = cache_allocate_sector(sector_idx, CACHE_READ);
          cache_read(cache, buffer+bytes_read, sector_ofs, chunk_size);
      } else {
          memset(buffer+bytes_read, chunk_size, 0);
      }
              
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
//  free (bounce);
    
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
//  uint8_t *bounce = NULL;
  uint8_t *cache = NULL;
    
  if (inode->deny_write_cnt)
    return 0;
//  PANIC("write_test: new: %d, sector: %d\n", offset+size, inode_length(inode));
  if (offset + size > inode_length(inode)) {
      lock_acquire(&inode->inode_lock);
      if (offset + size > inode_length(inode)) {
          size_t new_length = offset + size;
          void *cache = cache_allocate_sector(inode->sector, CACHE_WRITE);
          cache_write(cache, &new_length, 0, ENTRY_SIZE);
      }
      lock_release(&inode->inode_lock);
  }
   
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, true);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache = cache_allocate_sector(sector_idx, CACHE_WRITE);
      cache_write(cache, buffer+bytes_written, sector_ofs, chunk_size);
        
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
//  free (bounce);
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
  off_t *length = malloc(4);
  void *cache = cache_allocate_sector(inode->sector, CACHE_READ);
  cache_read(cache, length, 0, sizeof(length));
    PANIC("len: %d\n", *length);
  return *length;
}
