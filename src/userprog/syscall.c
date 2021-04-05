#include "userprog/syscall.h"
#include <stdio.h>
#include <debug.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);
static void load_arguments(int, char*, char**);

/* syscall handlers */
static void sys_halt(struct intr_frame *f, char* args);
static void sys_exit(struct intr_frame *f, char* args);
static void sys_exec(struct intr_frame *f, char* args);
static void sys_wait(struct intr_frame *f, char* args);
static void sys_create(struct intr_frame *f, char* args);
static void sys_remove(struct intr_frame *f, char* args);
static void sys_open(struct intr_frame *f, char* args);
static void sys_filesize(struct intr_frame *f, char* args);
static void sys_read(struct intr_frame *f, char* args);
static void sys_write(struct intr_frame *f, char* args);
static void sys_seek(struct intr_frame *f, char* args);
static void sys_tell(struct intr_frame *f, char* args);
static void sys_close(struct intr_frame *f, char* args);

static void
load_arguments(int argc, char* args, char** argv)
{
    for (int i = 0; i < argc; i++){
        memcpy(argv, &args, sizeof(args));
        *argv = args;
        args += sizeof(args);
        argv += 1;
    }
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  
  int syscall_no = *((int*)f->esp);
  
  char *argv = (char*)f->esp;
  argv += sizeof(syscall_no);
 
    printf("syscall %d ", syscall_no);
  if (syscall_no != SYS_WRITE) thread_exit();
    
  switch ((syscall_no)) {
      case SYS_HALT:
          sys_halt(f, argv);
          break;
      case SYS_EXIT:
          sys_exit(f, argv);
          break;
      case SYS_EXEC:
          sys_exec(f, argv);
          break;
      case SYS_WAIT:
          sys_wait(f, argv);
          break;
      case SYS_CREATE:
          sys_create(f, argv);
          break;
      case SYS_REMOVE:
          sys_remove(f, argv);
          break;
      case SYS_OPEN:
          sys_open(f, argv);
          break;
      case SYS_FILESIZE:
          sys_filesize(f, argv);
          break;
      case SYS_READ:
          sys_read(f, argv);
          break;
      case SYS_WRITE:
          sys_write(f, argv);
          break;
      case SYS_SEEK:
          sys_seek(f, argv);
          break;
      case SYS_TELL:
          sys_tell(f, argv);
          break;
      case SYS_CLOSE:
          sys_close(f, argv);
          break;
                
      default:
        break;
    }
  
}

static void
sys_halt(struct intr_frame *f, char* args)
{
    shutdown_power_off();
};

static void
sys_exit(struct intr_frame *f, char* args)
{
    int argc = 1;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    int status = *(int*)argv[0];
    
    struct thread *t = thread_current();
    t->tcb->exit_status = status;
    thread_exit();
};

static void sys_exec(struct intr_frame *f, char* args)
{
    int ret;
    
    int argc = 1;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    const char* cmd_line = *(char**)argv[0];
    
    tid_t child_tid = process_execute(cmd_line);
    
    /* fetch child thread tcb */
    struct thread* cur = thread_current();
    struct list_elem *e = list_front(&cur->child_list);
    struct thread_control_block *child_tcb;
    while(e != list_tail(&cur->child_list)) {
        child_tcb = list_entry(e, struct thread_control_block, elem);
        if (child_tcb->tid == child_tid) break;
        e = list_next(e);
    }
    ASSERT(e != list_tail(&cur->child_list));
            
    sema_down(&child_tcb->sema);
    ret = child_tcb->loaded ? child_tid : -1;
    
};

static void sys_wait(struct intr_frame *f, char* args)
{
    int argc = 1;
    char *argv[argc];
    load_arguments(argc, args, argv);
    tid_t child_tid = *(int*)argv[0];
    
    int ret = process_wait(child_tid);
    
};

static void sys_create(struct intr_frame *f, char* args)
{
    int argc = 2;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    const char* filename = *(char**)argv[0];
    uint32_t initial_size = *(uint32_t*)argv[1];
    
    bool ret = filesys_create(filename, initial_size);
    
};

static void sys_remove(struct intr_frame *f, char* args)
{
    int argc = 1;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    const char* filename = *(char**)argv[0];
    
    bool ret = filesys_remove(filename);
};

static void
sys_open(struct intr_frame *f, char* args)
{
    int argc = 1;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    const char* filename = *(char**)argv[0];
        
    int ret = -1;
    
    struct file *fp = filesys_open(filename);
    if (fp == NULL) return;
    
    ret = allocate_fd(fp);
    
};

static void
sys_filesize(struct intr_frame *f, char* args)
{
    int argc = 1;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    int fd = *(int*)argv[0];
    
    struct file* fp;
    fetch_file(fd, fp);
    
    int ret = fp == NULL ? 0 : file_length(fp);
    
};

static void
sys_read(struct intr_frame *f, char* args)
{
    int argc = 3;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    int fd = *(int*)argv[0];
    const char* buffer = *(char**)argv[1];
    int size = *(int*)argv[2];
    
    int bytes_read = 0;
    if (fd != 0){
        struct file* fp;
        fetch_file(fd, fp);
        bytes_read = fp == NULL ? -1 : file_read(fp, buffer, size);
    } else {
        while(bytes_read < size) {
            uint8_t key = input_getc();
            memcpy(buffer, &key, sizeof(key));
            buffer += sizeof(key);
            bytes_read += sizeof(key);
        }
    }
        
};

static void
sys_write(struct intr_frame *f, char* args)
{
    int argc = 3;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    int fd = *(int*)argv[0];
    const void* buffer = *(char**)argv[1];
    int size = *(int*)argv[2];
    
    int bytes_write;
    if(fd == 1) { // write to stdout
      putbuf(buffer, size);
      bytes_write = size;
    } else {
        struct file* fp;
        fetch_file(fd, fp);
        bytes_write = fp == NULL ? 0 : file_write(fp, buffer, size);
    }
    
};

static void
sys_seek(struct intr_frame *f, char* args)
{
    int argc = 2;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    int fd = *(int*)argv[0];
    uint32_t position = *(int*)argv[1];
    
    struct file* fp;
    fetch_file(fd, fp);
    file_seek(fp, position);
    
};

static void
sys_tell(struct intr_frame *f, char* args)
{
    int argc = 1;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    int fd = *(int*)argv[0];
    struct file* fp;
    fetch_file(fd, fp);
    
    int ret;
    if (fp != NULL) ret = file_tell(fp);
};

static void
sys_close(struct intr_frame *f, char* args)
{
    int argc = 1;
    char *argv[argc];
    load_arguments(argc, args, argv);
    
    int fd = *(int*)argv[0];
    struct file* fp;
    fetch_file(fd, fp);
    
    if (fp != NULL) file_close(fp);
    
};

