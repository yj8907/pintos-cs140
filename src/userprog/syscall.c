#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
static int load_arguments(void *esp, char**);

/* syscall handlers */
static void sys_halt(struct intr_frame *f, char** argv);
static void sys_exit(struct intr_frame *f, char** argv);
static void sys_exec(struct intr_frame *f, char** argv);
static void sys_wait(struct intr_frame *f, char** argv);
static void sys_create(struct intr_frame *f, char** argv);
static void sys_remove(struct intr_frame *f, char** argv);
static void sys_open(struct intr_frame *f, char** argv);
static void sys_filesize(struct intr_frame *f, char** argv);
static void sys_read(struct intr_frame *f, char** argv);
static void sys_write(struct intr_frame *f, char** argv);
static void sys_seek(struct intr_frame *f, char** argv);
static void sys_tell(struct intr_frame *f, char** argv);
static void sys_close(struct intr_frame *f, char** argv);

static int
load_arguments(void *esp, char** argv)
{
    int syscall_no = *((int*)esp);
    esp += sizeof(esp);
    for (int i = 0; i < numArg; i++){
        *(argv + i) = (char*)esp;
    }
    
    return syscall_no;
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
  char** argv;
  int syscall_no = load_arguments(f->esp, argv);
  printf("syscall no %d ", syscall_no);
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
sys_halt(struct intr_frame *f, char** argv)
{
    
};

static void
sys_exit(struct intr_frame *f, char** argv)
{
    
};

static void sys_exec(struct intr_frame *f, char** argv)
{
    
};

static void sys_wait(struct intr_frame *f, char** argv)
{
    
};

static void sys_create(struct intr_frame *f, char** argv)
{
    
};

static void sys_remove(struct intr_frame *f, char** argv)
{
    
};

static void
sys_open(struct intr_frame *f, char** argv)
{
    
};

static void
sys_filesize(struct intr_frame *f, char** argv)
{
    
};

static void
sys_read(struct intr_frame *f, char** argv)
{
    
};

static void
sys_write(struct intr_frame *f, char** argv)
{
    
};

static void
sys_seek(struct intr_frame *f, char** argv)
{
    
};

static void
sys_tell(struct intr_frame *f, char** argv)
{
    
};

static void
sys_close(struct intr_frame *f, char** argv)
{
    
};


static void
sys_wait(tid_t tid)
{
    
};
