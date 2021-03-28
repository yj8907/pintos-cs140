#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);

int sys_wait(tid_t tid);

#endif /* userprog/syscall.h */
