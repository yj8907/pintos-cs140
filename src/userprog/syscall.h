#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
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
    int closed;
};

void syscall_init (void);

#endif /* userprog/syscall.h */
