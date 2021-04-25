//
//  page.h
//  
//
//  Created by Yang Jiang on 4/18/21.
//

#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <bitmap.h>
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "lib/kernel/hash.h"

enum page_state
{
    VALID,
    ALLOCATED,
    SWAPPED
};

enum page_data_type
{
    ANONYMOUS,
    DISK_RDONLY,
    DISK_WRITE
};

struct vm_area
{
    struct hash_elem h_elem;
    size_t vm_start;
    size_t vm_end;
    enum page_data_type data_type;
    enum page_state state;
    /* swap location */
    uint32_t swap_location;
};

struct vm_mm_struct
{
    struct hash *mmap;
    uint32_t *user_ptr;
    uint32_t *kernel_ptr;
    uint32_t *end_ptr;
};

hash_hash_func vm_hash_hash_func;
hash_less_func vm_hash_less_func;
hash_action_func vm_hash_clear_func;

void *vm_alloc_page(void*, struct vm_mm_struct* vm_mm, size_t page_cnt, enum palloc_flags, enum page_data_type);

void *vm_mm_init(void);
void vm_update_page(struct thread* t, void* pg, enum page_state, uint32_t);
struct vm_area *vm_area_find(struct vm_mm_struct*, void* pg);

#endif /* page_h */
