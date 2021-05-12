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
#include "userprog/pagedir.h"
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

static size_t
compute_frame_number(void *frame)
{
    ASSERT (pg_ofs(frame) == 0);
    ASSERT ((size_t)vtop(frame)/PGSIZE < init_ram_pages);
    return (size_t)vtop(frame)/PGSIZE;
}

static struct vm_area*
fetch_vm_area_for_frame(struct frame_table_entry* fte)
{
    return vm_area_lookup(thread_current()->vm_mm, fte->virtual_page);
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
falloc_get_frame(void* vm_pg, void *eip, enum palloc_flags flags)
{
    static falloc_counter = 0;
    
    void *page = palloc_get_page(flags);
    void *new_frame;
    if (page == NULL) {
        falloc_counter += 1;
        new_frame = next_frame_to_evict(eip, 1);
        evict_frame(new_frame, 1);
        page = palloc_get_page(flags);
        
    }
    
    if (page == NULL) PANIC("no new frame available: %d, vm_pg: 0x%08x \n",
                                    flags & PAL_USER, vm_pg);
    
    ASSERT(pg_ofs(page) == 0);
    
    int frame_no = compute_frame_number(page);
    struct frame_table_entry* fte = frame_table+frame_no;
    
    ASSERT(fte->holder == NULL);
        
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
    pagedir_clear_page(fte->holder->pagedir, fte->virtual_page);

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
    
    struct vm_area *va = fetch_vm_area_for_frame(fte);
    if (va->data_type != DISK_RW) {
        swap_slot_t swap_slot = swap_allocate();
        swap_write(swap_slot, fte->virtual_page);
        
        vm_update_page(fte->holder, fte->virtual_page, ONDISK, swap_slot);
    } else if (va->data_type == DISK_RW) {
        ASSERT(va->file != NULL);
        if (pagedir_is_dirty(thread_current()->pagedir, va->vm_start))
            file_write_at(va->file, va->vm_start, va->content_bytes, va->file_pos);
        
        vm_update_page(fte->holder, fte->virtual_page, ONDISK, 0);
    }
    falloc_free_frame(frame);
}

void
load_frame(void *frame, size_t page_cnt)
{
    ASSERT(page_cnt == 1);
    
    ASSERT(frame != NULL);
    ASSERT(pg_ofs(frame) == 0);
    
    size_t frame_no = compute_frame_number(frame);
    struct frame_table_entry *fte = (frame_table+frame_no);
    struct vm_area *va = fetch_vm_area_for_frame(fte);
    
    ASSERT(va->state == ONDISK);
    ASSERT(va->data_type != DISK_RW);
    
    /* since virtual page has not been installed, need to use frame page */
    swap_read(va->swap_location, frame);
    swap_free(va->swap_location);
    
}

/* implment page replacement policy: second chance and working page pinning */
void*
next_frame_to_evict(void *eip, size_t page_cnt)
{
    ASSERT(page_cnt == 1);
    struct thread *cur = thread_current();
    
    struct frame_table_entry *fte = list_entry(list_front(&frame_in_use_queue), struct frame_table_entry, elem);
    while (pagedir_is_accessed(cur->pagedir, fte->virtual_page) ||
           pagedir_is_accessed(cur->pagedir, ptov(compute_frame_entry_no(fte)*PGSIZE)) ||
           pg_round_down(eip) == fte->virtual_page ) {
        
        fte = list_entry(list_pop_front(&frame_in_use_queue), struct frame_table_entry, elem);
        if (pg_round_down(eip) != fte->virtual_page) {
            pagedir_set_accessed(cur->pagedir, fte->virtual_page, false);
            pagedir_set_accessed(cur->pagedir, ptov(compute_frame_entry_no(fte)*PGSIZE), false);
        }
        list_push_back(&frame_in_use_queue, &fte->elem);
        
        fte = list_entry(list_front(&frame_in_use_queue), struct frame_table_entry, elem);
    }
        
    return ptov(compute_frame_entry_no(fte)*PGSIZE);
}

