//
//  cache.h
//  
//
//  Created by Yang Jiang on 5/14/21.
//

#ifndef cache_h
#define cache_h

#include "devices/block.h"

#define CACHE_NBLOCKS 64

enum cache_action
{
    NOOP,
    READ,
    WRITE
};

void cache_init(void);

void *cache_allocate_sector(block_sector_t, cache_action);

void cache_read(void *);
void cache_write(void *);

#endif /* cache_h */
