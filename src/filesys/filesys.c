#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"

#include "threads/thread.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  cache_init ();
    
  if (format) 
    do_format ();

  free_map_open ();
  /* initial main thread pwd as root dir */
  thread_current()->pwd = inode_open(ROOT_DIR_SECTOR);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}


static void
parse_filepath(const char *name, char **local_name, struct dir **dir)
{
    struct dir *curr_dir;
    struct inode *dir_inode;
        
    char *fullname, *filename, *saveptr;
    fullname = malloc(strlen(name) + 1);
    strlcpy(fullname, name, strlen(name) + 1);
      
    filename = strtok_r(fullname, "/", &saveptr);
    if (strcmp(filename, "") == 0) {
      curr_dir = dir_open_root ();
      filename = strtok_r(NULL, "/", &saveptr);
    }
    else {
      curr_dir = dir_open(inode_reopen(thread_current()->pwd));
    }
            
    /* search subdirectories */
    while(filename != NULL && curr_dir != NULL){
      if (!dir_lookup(curr_dir, filename, &dir_inode)) break;
      
      dir_close(curr_dir);
      if (inode_isdir(dir_inode)) {
        curr_dir = dir_open(dir_inode);
      }
      else {
          inode_close(dir_inode);
          curr_dir = NULL;
          break;
      }
      filename = strtok_r(NULL, "/", &saveptr);
    }
    
    if (filename == NULL || curr_dir == NULL) {
        if(curr_dir != NULL) dir_close(curr_dir);
        goto done;
    }
    
    char *next_filename = strtok_r(NULL, "/", &saveptr);
    if (next_filename != NULL) {
        if(curr_dir != NULL) dir_close(curr_dir);
        goto done;
    }

    *dir = malloc(sizeof(curr_dir));
    memcpy(*dir, curr_dir, sizeof(curr_dir));
    *local_name = malloc(strlen(filename)+1);
    memcpy(*local_name, filename, strlen(filename)+1);
    goto done;
    
    done:
      free(fullname);
      return;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir;
  char *filename;
    
  parse_filepath(name, &dir, &filename);
      
  PANIC("sector: %d\n", inode_sector(dir_get_inode(dir)));
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector, true)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  PANIC("test");
  if (dir != NULL) dir_close (dir);
  if (filename != NULL) free(filename);
  return success;
    
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
