#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

int sys_wait(pid_t pid);

#endif /* userprog/syscall.h */
