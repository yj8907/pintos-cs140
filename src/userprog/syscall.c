#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
static int load_arguments(void *esp, char*);

/* syscall handlers */
static void sys_halt(struct intr_frame *f, char* argv);
static void sys_exit(struct intr_frame *f, char* argv);
static void sys_exec(struct intr_frame *f, char* argv);
static void sys_wait(struct intr_frame *f, char* argv);
static void sys_create(struct intr_frame *f, char* argv);
static void sys_remove(struct intr_frame *f, char* argv);
static void sys_open(struct intr_frame *f, char* argv);
static void sys_filesize(struct intr_frame *f, char* argv);
static void sys_read(struct intr_frame *f, char* argv);
static void sys_write(struct intr_frame *f, char* argv);
static void sys_seek(struct intr_frame *f, char* argv);
static void sys_tell(struct intr_frame *f, char* argv);
static void sys_close(struct intr_frame *f, char* argv);

static int
load_arguments(void *esp, char* argv)
{
    
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
        
  printf("syscall no %d", syscall_no);
    
    int fd = *((int*)argv);
    printf("fd: %d ", fd);
    
    char print_output[4];
    argv += sizeof(fd);
    argv = (char**)argv;
    
    memcpy(print_output, (char*)(*argv), 2);
    
    char print_output2[4];
//    strlcpy(&print_output2, print_output, 3);
//    printf("buffer 0x%08x ", argv);
//    printf("buffer 0x%08x ", (char*)(*argv));
    printf("buffer %s ", print_output);
    
    argv = (char*)argv;
    argv += sizeof(argv);
    int sz = *((int*)argv);
    printf("size %d ", sz);
    
  thread_exit();
 
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
sys_halt(struct intr_frame *f, char* argv)
{
    
};

static void
sys_exit(struct intr_frame *f, char* argv)
{
    
};

static void sys_exec(struct intr_frame *f, char* argv)
{
    
};

static void sys_wait(struct intr_frame *f, char* argv)
{
    
};

static void sys_create(struct intr_frame *f, char* argv)
{
    
};

static void sys_remove(struct intr_frame *f, char* argv)
{
    
};

static void
sys_open(struct intr_frame *f, char* argv)
{
    
};

static void
sys_filesize(struct intr_frame *f, char* argv)
{
    
};

static void
sys_read(struct intr_frame *f, char* argv)
{
    
};

static void
sys_write(struct intr_frame *f, char* argv)
{
    
};

static void
sys_seek(struct intr_frame *f, char* argv)
{
    
};

static void
sys_tell(struct intr_frame *f, char* argv)
{
    
};

static void
sys_close(struct intr_frame *f, char* argv)
{
    
};

