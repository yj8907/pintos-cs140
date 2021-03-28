#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

int sys_wait(tid_t tid);

#endif /* userprog/syscall.h */
