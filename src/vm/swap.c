//
//  swap.c
//  
//
//  Created by Yang Jiang on 5/1/21.
//
#include <bitmap.h>
#include <round.h>
#include "lib/kernel/hash.h"

#include "devices/block.h"
#include "threads/palloc.h"
#include "threads/synch.h"

#include "swap.h"

static struct block* swap_block;
static block_sector_t swap_block_size;
static swap_slot_t swap_size;

static uint8_t nblock_pg;
static struct bitmap *used_map;
struct lock lock;

static block_sector_t slot_to_sector(swap_slot_t);

static block_sector_t
slot_to_sector(swap_slot_t slot)
{
    ASSERT( ((slot & SP_AREA) >> SP_SHIFT) == 0 );
    uint32_t slot_index = slot >> (SP_SHIFT + SP_AREABITS);
    block_sector_t sector_index = slot_index * nblock_pg;
    
    ASSERT (sector_index < swap_block_size);
    return sector_index;
}

void
swap_init(void)
{
    /* find swap block */
    struct block *block = NULL;
    for (block = block_first (); block != NULL; block = block_next (block)) {
        if (block_type (block) == BLOCK_SWAP) {
            swap_block = block;
            break;
        }
    }
    ASSERT(swap_block != NULL);
    
    swap_block_size = block_size(swap_block);
    
    nblock_pg = PGSIZE / BLOCK_SECTOR_SIZE;
    swap_size = swap_block_size/nblock_pg;
        
    size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (swap_size), PGSIZE);
    void *used_map_base = palloc_get_multiple(PAL_ZERO, bm_pages);
    used_map = bitmap_create_in_buf (swap_size, used_map_base, bm_pages * PGSIZE);
    
    lock_init (&lock);
    
}

void
swap_read(swap_slot_t slot, void *page)
{
    ASSERT(pg_ofs(page) == 0);
    block_sector_t sector = slot_to_sector(slot);
    for (size_t i = 0; i < nblock_pg; i++) {
        block_read(swap_block, sector, page);
        sector += 1;
        page += BLOCK_SECTOR_SIZE;
    }
}

void
swap_write(swap_slot_t slot, void *page)
{
    ASSERT(pg_ofs(page) == 0);
    block_sector_t sector = slot_to_sector(slot);
    for (size_t i = 0; i < nblock_pg; i++) {
        block_write(swap_block, sector, page);
        sector += 1;
        page += BLOCK_SECTOR_SIZE;
    }
}

swap_slot_t
swap_allocate(void)
{
    size_t swap_index;
    if (lock_held_by_current_thread(&lock)) PANIC("swap allocate");
//    lock_acquire (&lock);
    swap_slot_t slot_index = bitmap_scan_and_flip (used_map, 0, 1, false);
//    lock_release (&lock);
    
    uint32_t swap_area = 0;
    
    if (slot_index != BITMAP_ERROR)
        return slot_index << (SP_SHIFT + SP_AREABITS) + swap_area << SP_SHIFT;
    else
        PANIC("swap_allocate: out of slots");
};

void
swap_free(swap_slot_t slot_index)
{
    ASSERT(slot_index < swap_size);
    ASSERT (bitmap_all (used_map, slot_index, 1));
    bitmap_set_multiple (used_map, slot_index, 1, false);
}

