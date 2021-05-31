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
#include "devices/timer.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "filesys/filesys.h"

#include "cache.h"

struct cache_entry {
    struct list_elem elem;
    
    uint8_t score;
    bool dirty;
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
static void* fetch_new_cache_block(block_sector_t, enum cache_action);

static void init_cache_block(struct cache_entry*);
static void setup_cache_block(struct cache_entry*, size_t, enum cache_action);

static int cache_lookup(block_sector_t);
static size_t compute_cache_index(void *);
static void *cache_fetch_sector(block_sector_t, size_t, enum cache_action);

static struct list cache_in_use;
static struct list_elem *clock_iter;

static struct lock cache_lock;

static struct cache_entry *cache_table;
static struct thread *cache_flush_thread;

static void
init_cache_block(struct cache_entry* e)
{
    e->dirty = false;
    
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
    
    e->sector_no = block_sector;
    e->read_ref = 0;
    e->write_ref = 0;
    
    if (action == CACHE_READ) e->read_ref++;
    if (action == CACHE_WRITE) e->write_ref++;
    if (!lock_held) lock_release(&e->block_lock);
}

static void
cache_write_back(void)
{
    while(true){
        cache_flush();
        timer_sleep(1000);
    }
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
    
    cache_flush_thread = thread_create ("cache_flush_routine", PRI_DEFAULT, cache_write_back, NULL);
}

static size_t
compute_cache_index(void *cache)
{
    ASSERT( (cache - cache_base) % BLOCK_SECTOR_SIZE == 0);
    return (cache - cache_base) / BLOCK_SECTOR_SIZE;
}


static void *
cache_fetch_sector(block_sector_t block, size_t cache_index, enum cache_action action)
{
    struct cache_entry* e = cache_table + cache_index;
            
    int count = 0;
    lock_acquire(&e->block_lock);
    if (e->sector_no != block) {
        cache_index = -1;
        lock_release(&e->block_lock);
        return NULL;
    } else {
        if (action == CACHE_WRITE) {
            e->write_ref++;
            if (e->state != NOOP || e->write_ref > 1) {
                while (e->state != NOOP) {
//                    printf("cond_wait write: 0x%08x, count: %d, threadname: %s\n", cache_base+cache_index*BLOCK_SECTOR_SIZE, count, thread_name());
                    cond_wait(&e->write_cv, &e->block_lock);
                    count++;
                }
            }
        } else if (action == CACHE_READ) {
            e->read_ref++;
            if (e->write_ref > 0) {
                do {
//                    printf("cond_wait read: 0x%08x, count: %d, threadname: %s\n", cache_base+cache_index*BLOCK_SECTOR_SIZE, count, thread_name());
                    cond_wait(&e->read_cv, &e->block_lock);
                    count++;
                } while(e->state == CACHE_WRITE);
            }
        }
        
        e->state = action;
        lock_release(&e->block_lock);
        return cache_base + cache_index*BLOCK_SECTOR_SIZE;
    }
}


void*
cache_allocate_sector(block_sector_t block, enum cache_action action)
{
    int cache_index;
    lock_acquire (&cache_lock);
    cache_index = cache_lookup(block);
    lock_release (&cache_lock);
    
//    printf("cache_allocate_sector ckpt1\n");
    
    if (cache_index != -1){
        void *cache = cache_fetch_sector(block, cache_index, action);
        if (cache != NULL) return cache;
    }
    
//    printf("cache_allocate_sector ckpt2, %d\n", cache_index);
    /* obtain new block */    
    return fetch_new_cache_block(block, action);
}

void
cache_read(void *cache, void* buffer, size_t offset, size_t size)
{
    /* read data into memory */
    struct cache_entry* e = cache_table + compute_cache_index(cache);
    memcpy (buffer, cache + offset, size);
            
    lock_acquire(&e->block_lock);
    ASSERT(e->state == CACHE_READ);
    ASSERT(e->read_ref > 0);
    
    e->read_ref--;
    if (e->read_ref == list_size(&e->read_cv)) e->state = NOOP;
    
    if (e->write_ref > 0 && e->state == NOOP){
//        printf("cache_read cond signal write: 0x%08x, thread: %s\n", cache, thread_name());
        cond_signal(&e->write_cv, &e->block_lock);
    }
    else if (e->read_ref > 0 && e->state == NOOP) {
//        printf("cache_read cond signal read: 0x%08x, thread: %s\n", cache, thread_name());
        cond_signal(&e->read_cv, &e->block_lock);
    }
        
    lock_release(&e->block_lock);

}

void
cache_write(void *cache, void* buffer, size_t offset, size_t size)
{
    /* read data into memory */
    struct cache_entry* e = cache_table + compute_cache_index(cache);
    memcpy (cache+offset, buffer, size);

    lock_acquire(&e->block_lock);
    ASSERT(e->state == CACHE_WRITE);
    ASSERT(e->write_ref > 0);
    
    e->write_ref--;
    e->state = NOOP;
    e->dirty = true;
    if (e->read_ref > 0) {
//        printf("cache_write cond signal read: 0x%08x, thread: %s\n", cache, thread_name());
        cond_signal(&e->read_cv, &e->block_lock);
    }
    else if (e->write_ref > 0) {
//        printf("cache_write cond signal write: 0x%08x, thread: %s\n", cache, thread_name());
        cond_signal(&e->write_cv, &e->block_lock);
    }
    lock_release(&e->block_lock);

}

block_sector_t
cache_index_write(void *cache, uint32_t* sector, size_t offset)
{
    size_t size = 4;
    if (*(uint32_t*)(cache+offset) != BITMAP_ERROR) return *(uint32_t*)(cache+offset);
    
    cache_write(cache, sector, offset, size);
    return *sector;
}

static int
cache_lookup(block_sector_t block)
{
    int cache_index = -1;
    for (int i = 0; i < CACHE_NBLOCKS; i++){
        if ((cache_table+i)->sector_no == block) {
            cache_index = i;
            break;
        }
    }
    
    return cache_index;
}

static void*
fetch_new_cache_block(block_sector_t block, enum cache_action action)
{
    void *cache = NULL;
    
    /* obtain new block */
    size_t cache_index = bitmap_scan_and_flip (used_map, 0, 1, false);
    
    if (cache_index == BITMAP_ERROR) {
        evict_block();
        cache_index = bitmap_scan_and_flip (used_map, 0, 1, false);
        ASSERT(cache_index != BITMAP_ERROR);
    }
//    printf("fetch_new_cache_block ckpt2\n");
    lock_acquire (&cache_lock);
    /* check again if cache has been allocated for the block */
    int cache_check_idx = cache_lookup(block);
    while (cache_check_idx != -1) {
        lock_release (&cache_lock);
        cache = cache_fetch_sector(block, cache_check_idx, action);
        if (cache != NULL) {
            bitmap_set_multiple (used_map, cache_index, 1, false);
            return cache;
        }
        lock_acquire (&cache_lock);
        cache_check_idx = cache_lookup(block);
    }
//    printf("fetch_new_cache_block ckpt3\n");
    /* update cache state */
    setup_cache_block(cache_table+cache_index, block, action);
    list_push_back(&cache_in_use, &(cache_table+cache_index)->elem);
    lock_release (&cache_lock);
    
    cache = cache_base + BLOCK_SECTOR_SIZE*cache_index;
    block_read (fs_device, (cache_table+cache_index)->sector_no, cache);

    return cache;
}


static void
evict_block()
{
    bool dirty = false;
    block_sector_t sector_no;
    
    struct cache_entry *e;
    /* acquire block to evict */
    lock_acquire (&cache_lock);
    ASSERT(!list_empty(&cache_in_use));

    int counter = 0;
    while (true) {
        if (clock_iter == NULL || clock_iter == list_end(&cache_in_use)) clock_iter = list_front(&cache_in_use);
                    
        e = list_entry(clock_iter, struct cache_entry, elem);
        if (lock_try_acquire(&e->block_lock)){
//            if (e->read_ref == 0 && e->write_ref == 0 &&
//                (!e->dirty || counter > CACHE_NBLOCKS) ) break;
            if ( e->read_ref == 0 && e->write_ref == 0 ) break;
            lock_release(&e->block_lock);
        }
        counter++;
        clock_iter = list_next(clock_iter);
    }
    clock_iter = list_remove(clock_iter);
    lock_release (&cache_lock);

    /* evict block */
    /* write to disk if dirty */
    size_t cache_index = e - cache_table;
    dirty = e->dirty;
    sector_no = e->sector_no;
    setup_cache_block(e, -1, NOOP);
    lock_release(&e->block_lock);
    
    if (dirty) block_write (fs_device, sector_no, cache_base+cache_index*BLOCK_SECTOR_SIZE);
    memset(cache_base+cache_index*BLOCK_SECTOR_SIZE, 0, BLOCK_SECTOR_SIZE); /* set memory to all zeros*/
    
    /* free bitmap */
    ASSERT (bitmap_all (used_map, cache_index, 1));
    bitmap_set_multiple (used_map, cache_index, 1, false);
    
}

void
cache_flush(void)
{
    lock_acquire (&cache_lock);
    if (!list_empty(&cache_in_use)){
        struct cache_entry *e;
        struct list_elem *iter = list_front(&cache_in_use);
        while(iter != list_end(&cache_in_use)){
            e = list_entry(iter, struct cache_entry, elem);
            if (lock_try_acquire(&e->block_lock)){
                if (e->dirty) {
                    block_write (fs_device, e->sector_no, cache_base+(e - cache_table)*BLOCK_SECTOR_SIZE);
                    e->dirty = false;
                }
                lock_release(&e->block_lock);
            }
            iter = list_next(iter);
        }
    }
    lock_release (&cache_lock);
}


