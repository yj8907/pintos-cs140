#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "devices/input.h"

struct file_descriptor
{
    struct file* fp;
    struct list_elem elem;
    int fd_no;
};

void syscall_init (void);

#endif /* userprog/syscall.h */
