//
//  frame.h
//  
//
//  Created by Yang Jiang on 4/15/21.
//

#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "thraeds/palloc.h"
#include "thraeds/vaddr.h"
#include "threads/thread.h"
#include "vm/page.h"

typeof struct frame_table_entry {
    struct thread *holder;
    int numRef;
    void *virtual_page;
} fte;

void frame_table_init(void);

void *falloc_get_frame(enum palloc_flags);
void falloc_free_frame (void *);

void evict_frame(size_t page_cnt);
#endif /* frame_h */
