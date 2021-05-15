//
//  cache.c
//  
//
//  Created by Yang Jiang on 5/14/21.
//

#include <bitmap.h>
#include <round.h>
#include <list.h>

#include "lib/kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"

#include "cache.h"

static void *cache_base;
static void *used_map;

static size_t block_to_evict(void);
static void evict_block(size_t);

static size_t fetch_new_cache_block(void);

static void init_cache_block(size_t, size_t);
static void setup_cache_block(size_t, size_t);

static size_t cache_lookup(block_sector_t);

static list cache_in_use;
static struct lock cache_lock;

struct cache_entry {
    struct list_elem elem;
    
    uint8_t score;
    bool dirty;
    int sector_no;
    size_t cache_no;
    uint32_t ref;
    
    enum cache_action state;
    
    struct lock block_lock;
    struct condition read_cv;
    struct condition write_cv;
}

static struct cache_entry *cache_table;

void
cache_init(void)
{
    list_init (&cache_in_use);
    lock_init(&cache_lock);
    
    /* initialize cach table */
    cache_table = malloc(sizeof(struct cache_entry) * CACHE_NBLOCKS);
    for (size_t i = 0; i < CACHE_NBLOCKS; i++) init_cache_block(i);
    
    /* initialize cache */
    size_t cache_pages = DIV_ROUND_UP(BLOCK_SECTOR_SIZE * CACHE_NBLOCKS, PGSIZE);
    cache_base = palloc_get_multiple(PAL_ZERO, cache_pages);
    
    /* initialize cache bitmap */
    size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (CACHE_NBLOCKS), PGSIZE);
    void *used_map_base = palloc_get_multiple(PAL_ZERO, bm_pages);
    used_map = bitmap_create_in_buf (CACHE_NBLOCKS, used_map_base, bm_pages * PGSIZE);
}

void*
cache_allocate_sector(block_sector_t block, cache_action action)
{
    size_t cache_index;
    cache_index = cache_lookup(block);
    
    if (cache_index != -1){
        struct cache_entry* e = cache_table + cache_index;
        
        lock_acquire(&e->block_lock);
        
        if (e->sector_no != block) {
            cache_index = -1;
            lock_release(&e->block_lock);
        } else {
            if (action == WRITE) {
                while (e->state != NOOP) cond_wait(&write_cv, &e->block_lock);
                e->state = action;
            } else if (action == READ) {
                while(e->state == WRITE) cond_wait(&read_cv, &e->block_lock);
                e->state = action;
                e->ref++;
            }
                        
            lock_release(&e->block_lock);
            return cache_base + cache_index*BLOCK_SECTOR_SIZE;
        }
    }
    
    /* obtain new block */    
    size_t cache_index = fetch_new_cache_block();
    
    /* update cache state */
    setup_cache_block(cache_index, block, action);
    
    return cache_base + cache_index*BLOCK_SECTOR_SIZE;
}

static size_t
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
    lock_acquire (&cache_lock);
    size_t cache_index = bitmap_scan_and_flip (used_map, 0, 1, false);
    lock_release (&cache_lock);
    
    if (cache_index == BITMAP_ERROR) {
        size_t cache_block = block_to_evict();
        evict_block(cache_block);
        
        lock_acquire (&cache_lock);
        size_t cache_index = bitmap_scan_and_flip (used_map, 0, 1, false);
        lock_release (&cache_lock);
        ASSERT(cache_index != BITMAP_ERROR);
    }
}

static void
init_cache_block(size_t cache_index)
{
    struct cache_entry *e = cache_table + cache_index;
    e->dirty = false;
    e->cache_no = i;
    e->sector_no = -1;
    e->ref = 0;
    
    e->state = NOOP;
    lock_init(&e->block_lock);
    cond_init(&e->read_cv);
    cond_init(&e->write_cv);
}

static void
setup_cache_block(size_t cache_index, size_t block_sector, cache_action action)
{
    struct cache_entry* e = cache_table + cache_index;
    lock_acquire(&e->block_lock);
    
    e->state = action;
    e->dirty = false;
    e->cache_no = i;
    e->sector_no = -1;
    e->ref = 0;
    
    if (action == READ) e->ref++;
    lock_release(&e->block_lock);
}

