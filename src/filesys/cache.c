//
//  cache.c
//  
//
//  Created by Yang Jiang on 5/14/21.
//

#include <bitmap.h>
#include <round.h>
#include <list.h>
#include <string.h>

#include "lib/kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/filesys.h"

#include "cache.h"

struct cache_entry {
    struct list_elem elem;
    
    uint8_t score;
    bool dirty;
    bool loaded;
    int sector_no;
    
    uint32_t read_ref;
    uint32_t write_ref;
    
    enum cache_action state;
    
    struct lock block_lock;
    struct condition read_cv;
    struct condition write_cv;
};

static void *cache_base;
static void *used_map;

static void evict_block();
static size_t fetch_new_cache_block(void);

static void init_cache_block(struct cache_entry*);
static void setup_cache_block(struct cache_entry*, size_t, enum cache_action);

static int cache_lookup(block_sector_t);
static size_t compute_cache_index(void *);
static struct cache_entry* load_cache(void*);

static struct list cache_in_use;
static struct list_elem *clock_iter;

static struct lock cache_lock;

static struct cache_entry *cache_table;

static void
init_cache_block(struct cache_entry* e)
{
    e->dirty = false;
    e->loaded = false;
    
    e->sector_no = -1;
    e->read_ref = 0;
    e->write_ref = 0;
    
    e->state = NOOP;
    lock_init(&e->block_lock);
    cond_init(&e->read_cv);
    cond_init(&e->write_cv);
}

static void
setup_cache_block(struct cache_entry *e, size_t block_sector, enum cache_action action)
{
    bool lock_held = true;
    if (!lock_held_by_current_thread(&e->block_lock)) {
      lock_acquire(&e->block_lock);
      lock_held = false;
    }
    
    e->state = action;
    e->dirty = false;
    e->loaded = false;
    
    e->sector_no = block_sector;
    e->read_ref = 0;
    e->write_ref = 0;
    
    if (action == CACHE_READ) e->read_ref++;
    if (action == CACHE_WRITE) e->write_ref++;
    if (!lock_held) lock_release(&e->block_lock);
}

void
cache_init(void)
{
    list_init (&cache_in_use);
    clock_iter = NULL;
    
    lock_init(&cache_lock);
    
    /* initialize cach table */
    cache_table = malloc(sizeof(struct cache_entry) * CACHE_NBLOCKS);
    for (size_t i = 0; i < CACHE_NBLOCKS; i++) init_cache_block(cache_table+i);
    
    /* initialize cache */
    size_t cache_pages = DIV_ROUND_UP(BLOCK_SECTOR_SIZE * CACHE_NBLOCKS, PGSIZE);
    cache_base = palloc_get_multiple(PAL_ZERO, cache_pages);
    
    /* initialize cache bitmap */
    size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (CACHE_NBLOCKS), PGSIZE);
    void *used_map_base = palloc_get_multiple(PAL_ZERO, bm_pages);
    used_map = bitmap_create_in_buf (CACHE_NBLOCKS, used_map_base, bm_pages * PGSIZE);
}

static size_t
compute_cache_index(void *cache)
{
    ASSERT( (cache - cache_base) % BLOCK_SECTOR_SIZE == 0);
    return (cache - cache_base) / BLOCK_SECTOR_SIZE;
}

static struct cache_entry*
load_cache(void *cache)
{
    struct cache_entry* e = cache_table + compute_cache_index(cache);
    ASSERT(e->sector_no > -1);
    /* read data into memory */
    lock_acquire(&e->block_lock);
    if (!e->loaded) {
        printf("loading block: %d\n", e->sector_no);
        block_read (fs_device, e->sector_no, cache);
        e->loaded = true;
    }
    lock_release(&e->block_lock);
    
    return e;
}

void*
cache_allocate_sector(block_sector_t block, enum cache_action action)
{
    int cache_index;
    cache_index = cache_lookup(block);
    
    printf("cache_allocate_sector ckpt1\n");
    
    if (cache_index != -1){
        struct cache_entry* e = cache_table + cache_index;
        
        lock_acquire(&e->block_lock);
        
        if (e->sector_no != block) {
            cache_index = -1;
            lock_release(&e->block_lock);
        } else {
            
            if (action == CACHE_WRITE) {
                e->write_ref++;
                if (e->state != NOOP || e->write_ref > 1) {
                    while (e->state != NOOP) cond_wait(&e->write_cv, &e->block_lock);
                }
            } else if (action == CACHE_READ) {
                e->read_ref++;
                if (e->write_ref > 0) {
                    do { cond_wait(&e->read_cv, &e->block_lock);
                    } while(e->state == CACHE_WRITE);
                }
            }
            
            e->state = action;
            lock_release(&e->block_lock);
            return cache_base + cache_index*BLOCK_SECTOR_SIZE;
        }
    }
    
    /* obtain new block */    
    cache_index = fetch_new_cache_block();
    printf("cache_allocate_sector ckpt2, %d\n", cache_index);
    /* update cache state */
    ASSERT(cache_index > -1);
    setup_cache_block(cache_table+cache_index, block, action);
    printf("cache_allocate_sector ckpt3\n");
    return cache_base + cache_index*BLOCK_SECTOR_SIZE;
}

void
cache_read(void *cache, void* buffer, size_t offset, size_t size)
{
    /* read data into memory */
    printf("cache_read ckpt1, loaded: %d\n", (cache_table + compute_cache_index(cache))->loaded);
    struct cache_entry* e = load_cache(cache);
    memcpy (buffer, cache + offset, size);
    printf("cache_read ckpt2, loaded: %d, sector: %d\n", e->loaded,e->sector_no);
    
    lock_acquire(&e->block_lock);
    printf("cache_read ckpt3\n");
//    if (e->state != CACHE_READ) PANIC("state: %d\n", e->state);
    ASSERT(e->state == CACHE_READ);
    
    e->read_ref--;
    e->state = NOOP;
    if (e->write_ref > 0)
        cond_signal(&e->write_cv, &e->block_lock);
    else if (e->read_ref > 0)
        cond_signal(&e->read_cv, &e->block_lock);
    lock_release(&e->block_lock);
    printf("cache_read ckpt4\n");
}

void
cache_write(void *cache, void* buffer, size_t offset, size_t size)
{
//    printf("cache_write ckpt1\n");
    /* read data into memory */
    struct cache_entry* e = load_cache(cache);
    memcpy (cache+offset, buffer, size);
//    printf("cache_write ckpt2\n");
    lock_acquire(&e->block_lock);
//    printf("cache_write ckpt3\n");
    ASSERT(e->state == CACHE_WRITE);
    e->write_ref--;
    e->state = NOOP;
    e->dirty = true;
    if (e->read_ref > 0)
        cond_signal(&e->read_cv, &e->block_lock);
    else if (e->write_ref > 0)
        cond_signal(&e->write_cv, &e->block_lock);
    lock_release(&e->block_lock);
//    printf("cache_write ckpt4\n");
}


static int
cache_lookup(block_sector_t block)
{
    int cache_index = -1;
    
    lock_acquire (&cache_lock);
    for (int i = 0; i < CACHE_NBLOCKS; i++){
        if ((cache_table+i)->sector_no == block) {
            cache_index = i;
            break;
        }
    }
    lock_release (&cache_lock);
    
    return cache_index;
}

static size_t
fetch_new_cache_block(void)
{
    /* obtain new block */
    size_t cache_index = bitmap_scan_and_flip (used_map, 0, 1, false);
    
    if (cache_index == BITMAP_ERROR) {
        evict_block();
        cache_index = bitmap_scan_and_flip (used_map, 0, 1, false);
        ASSERT(cache_index != BITMAP_ERROR);
    }
    
    lock_acquire (&cache_lock);
    list_push_back(&cache_in_use, &(cache_table+cache_index)->elem);
    lock_release (&cache_lock);
    
    return cache_index;
}


static void
evict_block()
{
    struct cache_entry *e;
//    printf("evict_block ckpt1\n");
    /* acquire block to evict */
    lock_acquire (&cache_lock);
    ASSERT(!list_empty(&cache_in_use));
//    printf("evict_block ckpt2\n");
    while (true) {
        if (clock_iter == NULL) clock_iter = list_front(&cache_in_use);
                    
        e = list_entry(clock_iter, struct cache_entry, elem);
        if (lock_try_acquire(&e->block_lock)){
            if (e->read_ref == 0 && e->write_ref == 0) break;
            lock_release(&e->block_lock);
        }
        clock_iter = list_next(clock_iter);
    }
    clock_iter = list_remove(clock_iter);
    lock_release (&cache_lock);
//    printf("evict_block ckpt3\n");
    /* evict block */
    /* write to disk if dirty */
    size_t cache_index = e - cache_table;
    if (e->dirty) block_write (fs_device, e->sector_no, cache_base+cache_index*BLOCK_SECTOR_SIZE);
    memset(cache_base+cache_index*BLOCK_SECTOR_SIZE, 0, BLOCK_SECTOR_SIZE); /* set memory to all zeros*/
    
    setup_cache_block(e, -1, NOOP);
    lock_release(&e->block_lock);
//    printf("evict_block ckpt4\n");
    /* free bitmap */
    ASSERT (bitmap_all (used_map, cache_index, 1));
    bitmap_set_multiple (used_map, cache_index, 1, false);
    
}

