//
//  page.h
//  
//
//  Created by Yang Jiang on 4/18/21.
//

#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <bitmap.h>
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "lib/kernel/hash.h"
#include "filesys/file.h"

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
    DISK_RW
};

enum page_prot
{
    WRITE,
    RDONLY,
    WRITE_ON_COPY
};

struct vm_area
{
    struct hash_elem h_elem;
    void* vm_start;
    void* vm_end;
    enum page_data_type data_type;
    enum page_state state;
    enum page_prot protection;
    /* swap location */
    uint32_t swap_location;
    struct file* file;
    off_t file_pos;
    uint32_t content_bytes;
};

struct vm_mm_struct
{
    struct hash *mmap;
    uint32_t *user_ptr;
    uint32_t *kernel_ptr;
    uint32_t *end_ptr;
    void *esp;
};

hash_hash_func vm_hash_hash_func;
hash_less_func vm_hash_less_func;
hash_action_func vm_hash_clear_func;

void *vm_alloc_page(void*, struct vm_mm_struct* vm_mm, size_t page_cnt, enum palloc_flags, enum page_data_type,
                    struct file* file, uint32_t nbytes, bool);

void *vm_mm_init(void);
void vm_update_page(struct thread* t, void* pg, enum page_state, uint32_t);

struct vm_area *vm_area_lookup(struct vm_mm_struct*, void* pg);
bool is_vm_addr_valid(struct vm_mm_struct*, void* pg);

void page_not_present_handler(void *);
void vm_grow_stack(void *);

bool load_from_file(struct vm_area* va, void*);

#endif /* page_h */
