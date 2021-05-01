//
//  page.c
//
//
//  Created by Yang Jiang on 4/18/21.
//

#include "page.h"
#include <string.h>
#include "threads/pte.h"

static bool install_page (void *upage, void *kpage, bool writable);

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
//    uint32_t *kernel_free_ptr = ptov (init_ram_pages * PGSIZE);
    uint32_t *kernel_free_ptr = 0;
    uint32_t *user_free_ptr = 0;
    
    struct vm_mm_struct* vm_mm = palloc_get_page (0);
    
    if (vm_mm == NULL) {
        evict_frame(next_frame_to_evict(1), 1);
        vm_mm = palloc_get_page (0);
    }
    
    vm_mm->user_ptr = user_free_ptr;
    vm_mm->kernel_ptr = kernel_free_ptr;

    /* initialiez mmap has table */
    vm_mm->mmap = vm_mm + sizeof(struct vm_mm_struct);
    hash_init(vm_mm->mmap, &vm_hash_hash_func, &vm_hash_less_func, NULL);

    vm_mm->end_ptr = vm_mm + sizeof(struct vm_mm_struct) + sizeof(struct hash);
    
    return vm_mm;
}

void*
vm_mm_destroy(struct vm_mm_struct *vm_mm)
{
    /* iterate through mmap and clear frame table */
    struct hash_iterator i;
    hash_first (&i, vm_mm->mmap);
    while (hash_next (&i))
    {
        struct vm_area *va = hash_entry (hash_cur (&i), struct vm_area, h_elem);
        void *frame = vm_page_to_frame(thread_current()->pagedir, va->vm_start);
        printf("frame: 0x%08x\n", frame);
//        falloc_free_frame(frame);
    }
    
//    PANIC("vm_mm_destroy");
    palloc_free_page(vm_mm);
    
};

void*
vm_page_to_frame(uint32_t* pagedir, void* vm_page)
{
    uint32_t *pde = pagedir + pd_no(vm_page);
    uint32_t *pte = pde_get_pt (*pde) + pt_no(vm_page);
    void *frame = pte_get_page (*pte);
    return frame;
};


void*
vm_alloc_page(void *page, struct vm_mm_struct* vm_mm, size_t page_cnt,
              enum palloc_flags flags, enum page_data_type pg_type, struct file* file, uint32_t nbytes, bool writable)
{
//    void* page = flags & PAL_USER ? vm_mm->user_ptr : vm_mm->kernel_ptr;
//
//    if (flags & PAL_USER)
//        vm_mm->user_ptr += PGSIZE;
//    else
//       vm_mm->kernel_ptr += PGSIZE;

    
    static int vm_alloc_counter = 0;
    vm_alloc_counter += 1;
    
    ASSERT(vm_mm != NULL);
    
    if (pg_ofs(vm_mm->end_ptr) + sizeof(struct vm_area) >= PGSIZE)
        vm_mm->end_ptr = palloc_get_page(0);
            
    struct vm_area* vm_area_entry = vm_mm->end_ptr;
        
    vm_mm->end_ptr += sizeof(struct vm_area);
    
    vm_area_entry->vm_start = page;
    vm_area_entry->vm_end = page + PGSIZE;
    vm_area_entry->data_type = pg_type;
    vm_area_entry->state = VALID;
    vm_area_entry->protection = writable ? WRITE : RDONLY;
        
    vm_area_entry->file = file;
    if (file != NULL) vm_area_entry->file_pos = file_tell(file);
    vm_area_entry->content_bytes = nbytes;
    
    ASSERT(hash_insert(vm_mm->mmap, &vm_area_entry->h_elem) == NULL);
    
    return page;
}

               

void
vm_update_page(struct thread* t, void *pg, enum page_state next_state, uint32_t swap_location)
{
    
    ASSERT(pg_ofs(pg) == 0);
    
    struct vm_mm_struct *vm_mm = t->vm_mm;
    struct vm_area *va = vm_area_lookup(vm_mm, pg);
    
    if (va == NULL) return;
    
    va->state = next_state;
    if (next_state == SWAPPED) {
        ASSERT(swap_location != NULL);
        va->swap_location = swap_location;
    }
    
}
               
struct vm_area*
vm_area_lookup(struct vm_mm_struct* vm_mm, void* pg)
{
    ASSERT(pg_ofs(pg) == 0);
    struct vm_area va;
    va.vm_start = pg;
    struct hash_elem* elem = hash_find(vm_mm->mmap, &va.h_elem);
    
    if (elem != NULL)
        return hash_entry(elem, struct vm_area, h_elem);
    else
        return NULL;
}

bool
is_vm_addr_valid(struct vm_mm_struct* vm_mm, void* pg)
{
    struct vm_area *va = vm_area_lookup(vm_mm, pg_round_down(pg));
    if (va == NULL)
        return false;
    else
        return va->vm_start <= pg && pg < va->vm_end;
}

bool load_from_file(struct vm_area* va, void* kpage)
{
    if (kpage == NULL || va == NULL)
      return false;

    ASSERT(va->content_bytes <= PGSIZE);
    /* Load this page. */
    if (file_read_at (va->file, kpage, va->content_bytes, va->file_pos) != (int) va->content_bytes) return false;
    memset (kpage + va->content_bytes, 0, PGSIZE - va->content_bytes);
    return true;
}


void
page_not_present_handler(void *addr)
{
    void *page = pg_round_down(addr);
    struct vm_area *va = vm_area_lookup(thread_current()->vm_mm, page);
    
    if (va == NULL) force_exit();
    if (va->state == ALLOCATED) force_exit();
    
    if (va->state == VALID) {
        
        void *kpage = falloc_get_frame(page, is_user_vaddr(addr) ? PAL_USER | PAL_ZERO : PAL_ZERO);

        if (va->data_type != ANONYMOUS) {
            if (!load_from_file(va, kpage)) {
                force_exit();
            }
        }
        va->state = ALLOCATED;
        if (!install_page(page, kpage, va->protection == WRITE ? true : false)) force_exit();
    }
    else if (va->state == SWAPPED) {
        
    }
        
}

void
vm_grow_stack(void *addr)
{
    void *stack_pg = pg_round_down(addr);
    ASSERT (stack_pg  < PHYS_BASE);
    stack_pg = vm_alloc_page(stack_pg, thread_current()->vm_mm, 1, PAL_USER, ANONYMOUS, NULL, 0, true);
    page_not_present_handler(addr);

}


static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
