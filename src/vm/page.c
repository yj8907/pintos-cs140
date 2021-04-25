//
//  page.c
//
//
//  Created by Yang Jiang on 4/18/21.
//

#include "page.h"


unsigned
vm_hash_hash_func(const struct hash_elem *e, void *aux)
{
    struct vm_area* va = hash_entry(e, struct vm_area, h_elem);
    return hash_int(va->vm_start);
}

bool
vm_hash_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
    struct vm_area* va_a = hash_entry(a, struct vm_area, h_elem);
    struct vm_area* va_b = hash_entry(b, struct vm_area, h_elem);
    
    return hash_int(va_a->vm_start) < hash_int(va_b->vm_start);
}

void
vm_hash_clear_func(struct hash_elem *e, void *aux)
{
    struct vm_area* va = hash_entry(e, struct vm_area, h_elem);
}

void*
vm_mm_init(void)
{
    uint32_t *kernel_free_ptr = ptov (init_ram_pages * PGSIZE);
    uint32_t *user_free_ptr = 0;
    
    struct vm_mm_struct* vm_mm = palloc_get_page (0);
    
//    if (vm_mm == NULL) {
//        evict_frame(next_frame_to_evict(1), 1);
//        vm_mm = palloc_get_page (0);
//    }
    
    vm_mm->user_ptr = user_free_ptr;
    vm_mm->kernel_ptr = kernel_free_ptr;
    
    /* initialiez mmap has table */
    vm_mm->mmap = vm_mm + sizeof(struct vm_mm_struct);
    hash_init(vm_mm->mmap, &vm_hash_hash_func, &vm_hash_less_func, NULL);
    
    vm_mm->end_ptr = vm_mm + sizeof(struct vm_mm_struct) + sizeof(struct hash);
    
    return vm_mm;
}


void*
vm_alloc_page(void *page, struct vm_mm_struct* vm_mm, size_t page_cnt,
              enum palloc_flags flags, enum page_data_type pg_type)
{
//    void* page = flags & PAL_USER ? vm_mm->user_ptr : vm_mm->kernel_ptr;
//
//    if (flags & PAL_USER)
//        vm_mm->user_ptr += PGSIZE;
//    else
//       vm_mm->kernel_ptr += PGSIZE;

    if (pg_ofs(vm_mm->end_ptr) + sizeof(struct vm_area) >= PGSIZE)
        vm_mm->end_ptr = palloc_get_page(0);
        
    struct vm_area* vm_area_entry = vm_mm->end_ptr;
    vm_area_entry->vm_start = (uint32_t)page;
    vm_area_entry->vm_end = (uint32_t)page + PGSIZE;
    vm_area_entry->data_type = pg_type;
    vm_area_entry->state = VALID;
               
    ASSERT(hash_insert(vm_mm->mmap, &vm_area_entry->h_elem) == NULL);
    
    return page;
}

               

void
vm_update_page(struct thread* t, void *pg, enum page_state next_state, uint32_t swap_location)
{
    
    ASSERT(pg_ofs(pg) == 0);
    
    struct vm_mm_struct *vm_mm = t->vm_mm;
    struct vm_area *va = vm_area_find(vm_mm, pg);
    
    va->state = next_state;
    if (next_state == SWAPPED) {
        ASSERT(swap_location != NULL);
        va->swap_location = swap_location;
    }
    
}
               
struct vm_area*
vm_area_find(struct vm_mm_struct* vm_mm, void* pg)
{
    struct vm_area va;
    va.vm_start = (uint32_t)pg;
    struct hash_elem* elem = hash_find(vm_mm->mmap, &va.h_elem);
    
    ASSERT(elem != NULL);
    return hash_entry(elem, struct vm_area, h_elem);
}


