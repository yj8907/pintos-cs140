
#include "lib/user/syscall.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <debug.h>
#include <syscall-nr.h>
#include <string.h>
#include <round.h>

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "threads/palloc.h"
#include "threads/malloc.h"

#ifdef VM
#include "vm/page.h"
#endif

static int argc_max = 3;

typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;


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

static void validate_vaddr(void *addr, uint32_t);
static void validate_vaddr_write(void *addr, uint32_t);
static void validate_char_vaddr(void *addr);
static void validate_filename(void *addr);

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

static void sys_mmap(uint32_t *eax, char** argv);
static void sys_munmap(uint32_t *eax, char** argv);

static void sys_chdir(uint32_t *eax, char** argv);
static void sys_mkdir(uint32_t *eax, char** argv);
static void sys_readdir(uint32_t *eax, char** argv);
static void sys_isdir(uint32_t *eax, char** argv);
static void sys_inumber(uint32_t *eax, char** argv);

static char*
trim_dir_path(const char* name)
{
    char *dirname = name;
    size_t name_size = strlen(name);
    
    if (strcmp(name, "/") != 0 && strcmp(name+name_size-1, "/") == 0)  name_size--;
    dirname = malloc(name_size+1);
    strlcpy(dirname, name, name_size);
    strlcpy(dirname+name_size, "\0", 1);
    
    return dirname;
}

void
force_exit(void)
{
    int status = -1;
    char *argv[argc_max];
    argv[0] = &status;
    sys_exit(NULL, argv);
}

static void
validate_vaddr(void *addr, uint32_t sz)
{
    if (addr == NULL) force_exit();
    
    /* validate addr and addr+sz within user stack */
    if ( is_user_vaddr(addr) && is_user_vaddr(addr+sz) )  {
        for (int i = 0; i < sz; i++) {
            if (get_user(addr+i) == -1) force_exit();
        }
    } else {
        force_exit();
    }
}

static void
validate_vaddr_write(void *addr, uint32_t sz)
{
    if (addr == NULL) force_exit();
    
    /* validate addr and addr+sz within user stack */
    if ( is_user_vaddr(addr) && is_user_vaddr(addr+sz))  {
        int byte;
        for (int i = 0; i < sz; i++) {
            if ( (byte=get_user(addr+i)) == -1 ) force_exit();
            if (!put_user(addr+i, byte)) force_exit();
        }
    } else {
        force_exit();
    }
}


static void
validate_filename(void *filename)
{
    if (filename == NULL) force_exit();
    validate_char_vaddr(filename);
}

static void
validate_char_vaddr(void *addr)
{
    if (addr == NULL) force_exit();
    
    int byte_val;
    while ( is_user_vaddr(addr) && (byte_val = get_user(addr)) != -1){
        if ( (char)byte_val == '\0') break;
        addr++;
    }
    
    if ( !is_user_vaddr(addr) || byte_val == -1) force_exit();
}


static void
load_arguments(int argc, char* args, char** argv)
{
    validate_vaddr(args, sizeof(args)*argc);
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
  /* assign intr_frame to esp to thread esp to allow for userspace page fault in kernel */
  thread_current()->vm_mm->esp = f->esp;
    
  char *args = (char*)f->esp;
  
  validate_vaddr(args, sizeof(int));
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
      case SYS_MMAP:
          argc = 2;
          load_arguments(argc, args, argv);
          sys_mmap(eax, argv);
          break;
      case SYS_MUNMAP:
          sys_munmap(eax, argv);
          break;
      case SYS_CHDIR:
          sys_chdir(eax, argv);
          break;
      case SYS_MKDIR:
          sys_mkdir(eax, argv);
          break;
      case SYS_READDIR:
          argc = 2;
          load_arguments(argc, args, argv);
          sys_readdir(eax, argv);
          break;
      case SYS_ISDIR:
          sys_isdir(eax, argv);
          break;
      case SYS_INUMBER:
          sys_inumber(eax, argv);
          break;
          
      default:
        break;
    }
  
    thread_current()->vm_mm->esp = NULL;
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
    
    validate_char_vaddr(cmd_line);
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
    
    sema_down(&filesys_sema);
    int ret = filesys_create(filename, initial_size) ? 1 : 0;
    sema_up(&filesys_sema);
    
    memcpy(eax, &ret, sizeof(ret));
};

static void sys_remove(uint32_t *eax, char** argv)
{

    const char* filename = *(char**)argv[0];
    validate_filename(filename);
    
    sema_down(&filesys_sema);
    int ret = filesys_remove(filename) ? 1 : 0;
    sema_up(&filesys_sema);
    
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
        

    if (fp != NULL) {
        if ( (ret = allocate_fd(fp)) == -1) {
            sema_down(&filesys_sema);
            file_close(filename);
            sema_up(&filesys_sema);
        }
    }
    memcpy(eax, &ret, sizeof(ret));
};

static void
sys_filesize(uint32_t *eax, char** argv)
{
    int fd = *(int*)argv[0];
    
    struct file* fp = fetch_file(fd);
    sema_down(&filesys_sema);
    int ret = fp == NULL ? 0 : file_length(fp);
    sema_up(&filesys_sema);
    
    memcpy(eax, &ret, sizeof(ret));
};

static void
sys_read(uint32_t *eax, char** argv)
{
//    if (strcmp(thread_name(), "mmap-exit") == 0) PANIC("test");
    
    int fd_no = *(int*)argv[0];
    const char* buffer = *(char**)argv[1];
    uint32_t size = *(int*)argv[2];
    
    validate_vaddr_write(buffer, size);
    
    int bytes_read = 0;
    if (fd_no != 0 ){
        struct file* fp = fetch_file(fd_no);
//        sema_down(&filesys_sema);
        bytes_read = fp == NULL ? -1 : file_read(fp, buffer, size);
//        sema_up(&filesys_sema);
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
             
    validate_vaddr(buffer, size);
    
    struct file* fp;
    int bytes_write = 0;
    if (fd_no == 1) { // write to stdout
      putbuf(buffer, size);
      bytes_write = size;
    } else if ( (fp = fetch_file(fd_no)) != NULL ) {
//        sema_down(&filesys_sema);
        bytes_write = file_write(fp, buffer, size);
//        sema_up(&filesys_sema);
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
    free(fd);
};

static void
sys_mmap(uint32_t *eax, char** argv)
{
    int ret = MAP_FAILED;
    memcpy(eax, &ret, sizeof(ret));
    
    int fd_no = *(int*)argv[0];
    void* buffer = *(char**)argv[1];
    if (buffer == 0x0 || pg_ofs(buffer) != 0 || buffer == NULL) return;
            
    struct file* fp = fetch_file(fd_no);
    if (fp == NULL) return;
    
    size_t file_size = file_length(fp);
    size_t mmap_pages = DIV_ROUND_UP (file_size, PGSIZE);
    buffer = vm_alloc_page(buffer, thread_current()->vm_mm, mmap_pages, PAL_USER | PAL_ZERO, DISK_RW, fp, file_size, true);
    if (buffer == NULL) return;
    
    if ( (ret = allocate_mmapid(buffer, buffer + mmap_pages*PGSIZE)) == -1)
        ret = MAP_FAILED;
    
    memcpy(eax, &ret, sizeof(ret));
};


static void
sys_munmap(uint32_t *eax, char** argv)
{
    int mmap_no = *(int*)argv[0];
// PANIC("test: %d\n", mmap_no);
    struct mmap_descriptor* mmap_d = fetch_mmap(mmap_no);
    if (mmap_d == NULL) {
//        PANIC("test: %d\n", mmap_no);
        return;
    }
   
    for (void *pg = mmap_d->start_pg; pg < mmap_d->end_pg; pg += PGSIZE)
        vm_free_page(pg, thread_current()->vm_mm);
    
};


static void
sys_chdir(uint32_t *eax, char** argv)
{
    const char* input_dirname = *(char**)argv[0];
    validate_filename(input_dirname);
    char *dirname = trim_dir_path(input_dirname);
    
    bool success = false;
        
    struct inode* dir_inode;
    struct file *dir_file = filesys_open(dirname);
    if (dir_file != NULL) dir_inode = file_get_inode(dir_file);
    if (inode_isdir(dir_inode)) {
        inode_close(thread_current()->pwd);
        thread_current()->pwd = inode_reopen(dir_inode);
        success = true;
    }
    file_close(dir_file);
    
    memcpy(eax, &success, sizeof(success));
};


static void
sys_mkdir(uint32_t *eax, char** argv)
{
    const char* input_dirname = *(char**)argv[0];
    validate_filename(input_dirname);
    
    char *dirname = trim_dir_path(input_dirname);
    int success = 0;
    
    /* empty dir name is not allowed */
    if (strcmp(dirname, "") != 0 && strcmp(dirname, "/") != 0)
        success = filesys_create(dirname, 0);
    memcpy(eax, &success, sizeof(success));
    if (!success) return;
    PANIC("name:%d\n", success);
    struct file *dir_file = filesys_open(dirname);
    ASSERT(dir_file != NULL);
    
    struct inode* dir_inode = file_get_inode(dir_file);
    inode_setdir(dir_inode, true);
    file_close(dir_file);
    return;
}

static void sys_readdir(uint32_t *eax, char** argv)
{
    
}

static void sys_isdir(uint32_t *eax, char** argv)
{
    
}

static void sys_inumber(uint32_t *eax, char** argv)
{
    
}
