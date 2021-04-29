//
//  frame.c
//  
//
//  Created by Yang Jiang on 4/15/21.
//

#include "frame.h"
#include <round.h>

static size_t frame_table_page_cnt;
static struct frame_table_entry *frame_table;
static struct list frame_in_use_queue;

static size_t compute_frame_entry_no(struct frame_table_entry* ptr);

static size_t
compute_frame_entry_no(struct frame_table_entry* ptr)
{
    ASSERT ( (size_t)(ptr - frame_table) % sizeof(struct frame_table_entry) == 0);
    return (size_t)(ptr - frame_table) / sizeof(struct frame_table_entry);
}

static size_t compute_frame_number(void *frame)
{
    ASSERT (pg_ofs(frame) == 0);
    ASSERT ((size_t)vtop(frame)/PGSIZE < init_ram_pages);
    return (size_t)vtop(frame)/PGSIZE;
}

void
frame_init(void)
{
    frame_table_page_cnt = DIV_ROUND_UP(init_ram_pages*sizeof(struct frame_table_entry), PGSIZE);
    frame_table = palloc_get_multiple(PAL_ASSERT | PAL_ZERO, frame_table_page_cnt);
    
    list_init (&frame_in_use_queue);
}

/* frame is accessed through virtual addressing */
void*
falloc_get_frame(void* vm_pg, enum palloc_flags flags)
{
    void *page = palloc_get_page(flags);
    if (page == NULL) {
        PANIC("null page: 0x%08x\n", vm_pg);
        void *new_frame = next_frame_to_evict(1);
        evict_frame(new_frame, 1);
        void *page = palloc_get_page(flags);
    }
    
    if (page == NULL) thread_exit();
    ASSERT(pg_ofs(page) == 0);
    
    int frame_no = compute_frame_number(page);
    
//    ASSERT((frame_table+frame_no)->holder == NULL);
    if ((frame_table+frame_no)->holder != NULL) {
        printf("thread name: %s\n", (frame_table+frame_no)->holder->name);
        printf("page: 0x%08x\n", page);
    }
    (frame_table+frame_no)->holder = thread_current();
    (frame_table+frame_no)->numRef = 1;
    (frame_table+frame_no)->virtual_page = vm_pg;
    
    list_push_back(&frame_in_use_queue, &(frame_table+frame_no)->elem);
    
    return page;
}

void falloc_free_frame(void *frame)
{
    if (frame == NULL) return;
    
    ASSERT(pg_ofs(frame) == 0);
    
    size_t frame_no = compute_frame_number(frame);
    (frame_table+frame_no)->holder = NULL;
    (frame_table+frame_no)->numRef = 0;
    (frame_table+frame_no)->virtual_page = NULL;
    
    /* remvove frame from page replacement queue */
    list_remove(&(frame_table+frame_no)->elem);
    
    palloc_free_page(frame);
}

void
evict_frame(void *frame, size_t page_cnt)
{
    ASSERT(page_cnt == 1);
    
    ASSERT(frame != NULL);
    ASSERT(pg_ofs(frame) == 0);
    
    size_t frame_no = compute_frame_number(frame);
    
    ASSERT((frame_table+frame_no)->holder != NULL);
    
    uint32_t swap_location = NULL; /* need to implement swapping */
    vm_update_page((frame_table+frame_no)->holder, (frame_table+frame_no)->virtual_page,
                   SWAPPED, swap_location);
    
    falloc_free_frame(frame);
}

/* implment page replacement policy */
void*
next_frame_to_evict(size_t page_cnt)
{
    ASSERT(page_cnt == 1);
    struct frame_table_entry *fte = list_entry(list_front(&frame_in_use_queue), struct frame_table_entry, elem);
    
//    return ptov(compute_frame_entry_no(fte)*PGSIZE);
    return NULL;
}

