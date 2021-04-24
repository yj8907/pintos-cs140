//
//  frame.c
//  
//
//  Created by Yang Jiang on 4/15/21.
//

#include "frame.h"
#include <round.h>

static struct frame_table_entry *frame_table;
static size_t frame_table_page_cnt;

static size_t compute_frame_number(void *frame)
{
    uint8_t *free_start = ptov (1024 * 1024);
    ASSERT (pg_ofs(frame) == 0);
    return (frame - free_start)/PGSIZE;
}

void
frame_table_init(void)
{
    frame_table_page_cnt = DIV_ROUND_UP(init_ram_pages*sizeof(struct frame_table_entry), PGSIZE);
    frame_table = palloc_get_multiple(PAL_ASSERT | PAL_ZERO, frame_table_page_cnt);
}

/* frame is accessed through virtual addressing */
void*
falloc_get_frame(void* vm_pg, enum palloc_flags)
{
    void *page = palloc_get_page(palloc_flags);
    if (page == NULL) {
        evict_frame(1);
        void *page = palloc_get_page(palloc_flags);
    }
    if (page == NULL) thread_exit();
    ASSERT(pg_ofs(page) == 0);
    
    int frame_no = compute_frame_number(page);
    
    ASSERT((frame_table+frame_no)->holder == NULL);
    (frame_table+frame_no)->holder = thread_current();
    (frame_table+frame_no)->numRef = 1;
    (frame_table+frame_no)->virtual_page = vm_pg;
    
    return page;
}

void
evict_frame(void *frame, size_t page_cnt)
{
    ASSERT(page_cnt == 1);
    
    ASSERT(frame != NULL);
    ASSERT(pg_ofs(frame) == 0);
    
    int frame_no = compute_frame_number(frame);
    
    ASSERT((frame_table+frame_no)->holder != NULL);
    
    uint32_t swap_location = NULL; /* need to implement swapping */
    vm_update_page((frame_table+frame_no)->holder, (frame_table+frame_no)->virtual_page,
                   SWAPPED, swap_location);
    
    (frame_table+frame_no)->holder = NULL;
    (frame_table+frame_no)->numRef = 0;
    (frame_table+frame_no)->virtual_page = NULL;
    
}

