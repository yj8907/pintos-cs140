//
//  swap.h
//  
//
//  Created by Yang Jiang on 5/1/21.
//

#ifndef swap_h
#define swap_h

#include <bitmap.h>
#include <round.h>
#include "lib/kernel/hash.h"

#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/synch.h"

#define SP_SHIFT 1
#define SP_AREABITS 7
#define SP_AREA BITMASK(SP_SHIFT, SP_AREABITS)

typedef uint32_t swap_slot_t;

void swap_init(void);

void swap_read(swap_slot_t, void*);
void swap_write(swap_slot_t, void*);

swap_slot_t swap_allocate(void);
void swap_free(swap_slot_t);

#endif /* swap_h */
