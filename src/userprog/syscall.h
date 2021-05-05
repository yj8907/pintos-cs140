#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "threads/synch.h"

#ifdef VM
#include "vm/page.h"
#endif

#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "devices/input.h"

/* synchronize file access */
struct semaphore filesys_sema;

struct file_descriptor
{
    struct file* fp;
    struct list_elem elem;
    int fd_no;
};

struct mmap_descriptor
{
    void *start_pg;
    void *end_pg;
    int mmap_no;
    
    struct list_elem elem;
};

void syscall_init (void);
void force_exit(void);

#endif /* userprog/syscall.h */
