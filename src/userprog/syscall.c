#include "userprog/syscall.h"
#include <stdio.h>
#include <debug.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/file.h"

static int argc_max = 3;

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static void syscall_handler (struct intr_frame *);
static void load_arguments(int, char*, char**);

static void validate_vaddr(void *addr, int, bool);
static void validate_filename(void *addr);

static void force_exit(void);

/* syscall handlers */
static void sys_halt(uint32_t *eax, char** argv);
static void sys_exit(uint32_t *eax, char** argv);
static void sys_exec(uint32_t *eax, char** argv);
static void sys_wait(uint32_t *eax, char** argv);
static void sys_create(uint32_t *eax, char** argv);
static void sys_remove(uint32_t *eax, char** argv);
static void sys_open(uint32_t *eax, char** argv);
static void sys_filesize(uint32_t *eax, char** argv);
static void sys_read(uint32_t *eax, char** argv);
static void sys_write(uint32_t *eax, char** argv);
static void sys_seek(uint32_t *eax, char** argv);
static void sys_tell(uint32_t *eax, char** argv);
static void sys_close(uint32_t *eax, char** argv);

static void
force_exit(void)
{
    printf("force exit");
    int status = -1;
    char *argv[argc_max];
    argv[0] = &status;
    sys_exit(NULL, argv);
}

static void
validate_filename(void *filename)
{
    if (filename == NULL) force_exit();
    validate_vaddr(filename, -1, false);
}

static void
validate_vaddr(void *addr, int sz, bool write)
{
    
    if ( !is_user_vaddr(addr) || (sz >= 0 && !is_user_vaddr(addr+sz)) ) force_exit();
        
    int count = 0;
    if (sz >= 0){
        while ( count < sz && get_user(addr) != -1){
            if (write && !put_user(addr,  0)) break;
            addr++; count++;
        }
    } else {
        while ( is_user_vaddr(addr) && get_user(addr) != -1){
            if ( (char)get_user(addr) == '\0') break;
            if (write && !put_user(addr,  0)) break;
            addr++;
        }
    }

    if ( !is_user_vaddr(addr) || get_user(addr) == -1 ) force_exit();
    if (write && !put_user(addr,  0)) force_exit();
    printf("validate");
}


static void
load_arguments(int argc, char* args, char** argv)
{
    validate_vaddr(args, sizeof(args)*argc, false);
    for (int i = 0; i < argc; i++){
        *argv = args;
        args += sizeof(args);
        argv += 1;
    }
}

void
syscall_init (void) 
{
  sema_init(&filesys_sema, 1);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  char *args = (char*)f->esp;
  
  validate_vaddr(args, sizeof(int), false);
  int syscall_no = *((int*)args);
  args += sizeof(syscall_no);
      
  uint32_t *eax = &f->eax;
    
  int argc = 1;
  char *argv[argc_max];
  load_arguments(argc, args, argv);
    
  switch ((syscall_no)) {
      case SYS_HALT:
          sys_halt(eax, argv);
          break;
      case SYS_EXIT:
          sys_exit(eax, argv);
          break;
      case SYS_EXEC:
          sys_exec(eax, argv);
          break;
      case SYS_WAIT:
          sys_wait(eax, argv);
          break;
      case SYS_CREATE:
          argc = 2;
          load_arguments(argc, args, argv);
          sys_create(eax, argv);
          break;
      case SYS_REMOVE:
          sys_remove(eax, argv);
          break;
      case SYS_OPEN:
          sys_open(eax, argv);
          break;
      case SYS_FILESIZE:
          sys_filesize(eax, argv);
          break;
      case SYS_READ:
          argc = 3;
          load_arguments(argc, args, argv);
          sys_read(eax, argv);
          break;
      case SYS_WRITE:
          argc = 3;
          load_arguments(argc, args, argv);
          sys_write(eax, argv);
          break;
      case SYS_SEEK:
          argc = 2;
          load_arguments(argc, args, argv);
          sys_seek(eax, argv);
          break;
      case SYS_TELL:
          sys_tell(eax, argv);
          break;
      case SYS_CLOSE:
          sys_close(eax, argv);
          break;
                
      default:
        break;
    }
  
}

static void
sys_halt(uint32_t *eax, char** argv)
{
    shutdown_power_off();
};

static void
sys_exit(uint32_t *eax, char** argv)
{

    int status = *(int*)argv[0];
    
    struct thread *t = thread_current();
    printf ("%s: exit(%d)\n", thread_name(), status);
    t->tcb->exit_status = status;
    thread_exit();
};

static void sys_exec(uint32_t *eax, char** argv)
{
    int ret = -1;
    const char* cmd_line = *(char**)argv[0];
    
    validate_vaddr(cmd_line, -1, false);
    
    tid_t child_tid = process_execute(cmd_line);
    
    /* if process_create fails */
    memcpy(eax, &ret, sizeof(ret));
    if (child_tid == TID_ERROR) return;
    
    /* fetch child thread tcb and wait for it to load sucessfully */
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
    sema_up(&child_tcb->sema);
    
    memcpy(eax, &ret, sizeof(ret));
};

static void sys_wait(uint32_t *eax, char** argv)
{
    
    tid_t child_tid = *(int*)argv[0];
    
    int ret = process_wait(child_tid);
    memcpy(eax, &ret, sizeof(ret));
};

static void sys_create(uint32_t *eax, char** argv)
{

    const char* filename = *(char**)argv[0];
    uint32_t initial_size = *(uint32_t*)argv[1];
    
    validate_filename(filename);
    
    int ret = filesys_create(filename, initial_size) ? 1 : 0;
    memcpy(eax, &ret, sizeof(ret));
};

static void sys_remove(uint32_t *eax, char** argv)
{

    const char* filename = *(char**)argv[0];
    validate_filename(filename);
    
    int ret = filesys_remove(filename) ? 1 : 0;
    memcpy(eax, &ret, sizeof(ret));
};

static void
sys_open(uint32_t *eax, char** argv)
{
    const char* filename = *(char**)argv[0];
    validate_filename(filename);
    
    int ret = -1;
    
    sema_down(&filesys_sema);
    struct file *fp = filesys_open(filename);
    sema_up(&filesys_sema);
    
    if (fp != NULL) ret = allocate_fd(fp);
    
    memcpy(eax, &ret, sizeof(ret));
};

static void
sys_filesize(uint32_t *eax, char** argv)
{
    int fd = *(int*)argv[0];
    
    struct file* fp = fetch_file(fd);
    int ret = fp == NULL ? 0 : file_length(fp);
    memcpy(eax, &ret, sizeof(ret));
};

static void
sys_read(uint32_t *eax, char** argv)
{
    int fd_no = *(int*)argv[0];
    const char* buffer = *(char**)argv[1];
    uint32_t size = *(int*)argv[2];
    
    validate_vaddr(buffer, size, false);
    
    int bytes_read = 0;
    if (fd_no != 0 ){
        struct file* fp = fetch_file(fd_no);
        bytes_read = fp == NULL ? -1 : file_read(fp, buffer, size);
    } else {
        while(bytes_read < size) {
            uint8_t key = input_getc();
            memcpy(buffer, &key, sizeof(key));
            buffer += sizeof(key);
            bytes_read += sizeof(key);
        }
    }
    memcpy(eax, &bytes_read, sizeof(bytes_read));
};

static void
sys_write(uint32_t *eax, char** argv)
{
    int fd_no = *(int*)argv[0];
    const void* buffer = *(char**)argv[1];
    uint32_t size = *(int*)argv[2];
    
    validate_vaddr(buffer, size, true);
    
    int bytes_write = 0;
    if (fd_no == 1) { // write to stdout
      putbuf(buffer, size);
      bytes_write = size;
    } else {
        struct file* fp = fetch_file(fd_no);
        bytes_write = fp == NULL ? 0 : file_write(fp, buffer, size);
    }
    memcpy(eax, &bytes_write, sizeof(bytes_write));
};

static void
sys_seek(uint32_t *eax, char** argv)
{
    int fd = *(int*)argv[0];
    uint32_t position = *(int*)argv[1];
    
    struct file* fp = fetch_file(fd);
    if (fp != NULL) file_seek(fp, position);
    
};

static void
sys_tell(uint32_t *eax, char** argv)
{
    int fd = *(int*)argv[0];
    struct file* fp = fetch_file(fd);
    
    int ret = 0;
    if (fp != NULL) ret = file_tell(fp);
    memcpy(eax, &ret, sizeof(ret));
};

static void
sys_close(uint32_t *eax, char** argv)
{
    int fd_no = *(int*)argv[0];
    struct file_descriptor* fd = fetch_file_descriptor(fd_no);
    if (fd == NULL) return;
    
    struct file* fp = fd->fp;
    
    sema_down(&filesys_sema);
    if (fp != NULL) file_close(fp);
    sema_up(&filesys_sema);
    
    list_remove(&fd->elem);
    palloc_free_page(fd);
};

