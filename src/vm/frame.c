//
//  frame.c
//  
//
//  Created by Yang Jiang on 4/15/21.
//

#include <round.h>
#include "lib/kernel/hash.h"

#include "frame.h"
#include "threads/pte.h"
#include "vm/page.h"
#include "vm/swap.h"

static size_t frame_table_page_cnt;
static struct frame_table_entry *frame_table;
static struct list frame_in_use_queue;

static size_t compute_frame_entry_no(struct frame_table_entry* ptr);

static size_t
compute_frame_entry_no(struct frame_table_entry* ptr)
{
    return (size_t)(ptr - frame_table);
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

static inline bool
is_tail (struct list_elem *elem)
{
  return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* frame is accessed through virtual addressing */
void*
falloc_get_frame(void* vm_pg, enum palloc_flags flags)
{
    static falloc_counter = 0;
    
    void *page = palloc_get_page(flags);
    if (page == NULL) {
        falloc_counter += 1;
//        PANIC("null page: 0x%08x, mmap size: %d \n", vm_pg, hash_size(thread_current()->vm_mm->mmap));
        void *new_frame = next_frame_to_evict(1);
        evict_frame(new_frame, 1);
        
        void *page = palloc_get_page(flags);
    }
    
    if (page == NULL) PANIC("no new frame available: %d, vm_pg: 0x%08x \n", flags & PAL_USER, vm_pg);
    ASSERT(pg_ofs(page) == 0);
    
    int frame_no = compute_frame_number(page);
    struct frame_table_entry* fte = frame_table+frame_no;
    
//    ASSERT(fte->holder == NULL);
    if (fte->holder != NULL) {
        printf("thread name: %s\n", (frame_table+frame_no)->holder->name);
        printf("page: 0x%08x\n", page);
        printf("frameno: %d\n", frame_no);
    }

    fte->holder = thread_current();
    fte->numRef = 1;
    fte->virtual_page = vm_pg;
    
    list_push_back(&frame_in_use_queue, &fte->elem);
    
    return page;
}

void falloc_free_frame(void *frame)
{

    if (frame == NULL) return;
    
    ASSERT(pg_ofs(frame) == 0);
    
    struct frame_table_entry* fte = frame_table + compute_frame_number(frame);
    ASSERT(fte->holder != NULL && fte->virtual_page != NULL);
    
    /* free the page and update pagedir */
    uint32_t *pde = fte->holder->pagedir + pd_no(fte->virtual_page);
    ASSERT(*pde & PTE_P);
    uint32_t *pte = pde_get_pt (*pde) + pt_no(fte->virtual_page);
    ASSERT(*pte & PTE_P);
    *pte = *pte & !PTE_P;
    
    palloc_free_page(frame);
    
    fte->holder = NULL;
    fte->numRef = 0;
    fte->virtual_page = NULL;
    
    /* remvove frame from page replacement queue */
    list_remove(&fte->elem);
}

void
evict_frame(void *frame, size_t page_cnt)
{
    ASSERT(page_cnt == 1);
    
    ASSERT(frame != NULL);
    ASSERT(pg_ofs(frame) == 0);
    
    size_t frame_no = compute_frame_number(frame);
    struct frame_table_entry *fte = (frame_table+frame_no);
    ASSERT(fte->holder != NULL && fte->virtual_page != NULL);
    
    swap_slot_t swap_slot = swap_allocate();
    swap_write(swap_slot, fte->virtual_page);
    
    vm_update_page(fte->holder, fte->virtual_page, SWAPPED, swap_slot);
    
    falloc_free_frame(frame);
}

/* implment page replacement policy */
void*
next_frame_to_evict(size_t page_cnt)
{
    ASSERT(page_cnt == 1);
    struct frame_table_entry *fte = list_entry(list_front(&frame_in_use_queue), struct frame_table_entry, elem);
    
    return ptov(compute_frame_entry_no(fte)*PGSIZE);
}

