//
//  frame.h
//  
//
//  Created by Yang Jiang on 4/15/21.
//

#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>

#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"

struct frame_table_entry {
    struct thread *holder;
    int numRef;
    void *virtual_page;
    
    struct list_elem elem;
};

void frame_init(void);

void *falloc_get_frame(void *, enum palloc_flags);
void falloc_free_frame (void *);

void evict_frame(void*, size_t page_cnt);
void load_frame(void*, size_t page_cnt);

void* next_frame_to_evict(size_t page_cnt);

#endif /* frame_h */
