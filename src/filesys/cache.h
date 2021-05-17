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
    CACHE_READ,
    CACHE_WRITE
};

void cache_init(void);

void *cache_allocate_sector(block_sector_t, enum cache_action);

void cache_read(void *, void*, size_t, size_t);
void cache_write(void *, void*, size_t, size_t);

#endif /* cache_h */
