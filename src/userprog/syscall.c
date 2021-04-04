#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

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
  printf ("system call!\n");
  
  int syscall_no = *((int*)f->esp);
  
  char *argv = (char*)f->esp;
  argv += sizeof(syscall_no);
 
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
    
};

static void
sys_exit(struct intr_frame *f, char* args)
{
    
};

static void sys_exec(struct intr_frame *f, char* args)
{
    
};

static void sys_wait(struct intr_frame *f, char* args)
{
    
};

static void sys_create(struct intr_frame *f, char* args)
{
    
};

static void sys_remove(struct intr_frame *f, char* args)
{
    
};

static void
sys_open(struct intr_frame *f, char* args)
{
    
};

static void
sys_filesize(struct intr_frame *f, char* args)
{
    
};

static void
sys_read(struct intr_frame *f, char* args)
{
    
};

static void
sys_write(struct intr_frame *f, char* args)
{
    int numArg = 3;
    char *argv[numArg];
    load_arguments(numArg, args, argv);
    
    int fd = *(int*)args;
    args += sizeof(fd);
    
    const void* buffer = *(char**)args;
    args += sizeof(buffer);
    
    int size = *(int*)args;
    
    int fd = *(int*)argv[0];
//    const void* buffer = *(char**)argv[1];
//    int size = *(int*)argv[2];
    
    int ret;
    if(fd == 1) { // write to stdout
      putbuf(buffer, size);
      ret = size;
    }
    
};

static void
sys_seek(struct intr_frame *f, char* args)
{
    
};

static void
sys_tell(struct intr_frame *f, char* args)
{
    
};

static void
sys_close(struct intr_frame *f, char* args)
{
    
};

