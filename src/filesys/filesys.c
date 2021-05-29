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

static char*
trim_dir_path(const char* name)
{
    char *dirname = name;
    size_t name_size = strlen(name);
    
    if (strcmp(name, "/") != 0 && strcmp(name+name_size-1, "/") == 0) name_size--;
    dirname = malloc(name_size+1);
    strlcpy(dirname, name, name_size+1);
    
    return dirname;
}

struct dir*
parse_filepath(const char *name, char **local_name, bool create)
{
    char *pathsep = "/";
    struct dir *curr_dir, *prev_dir;
    struct inode *dir_inode = NULL;
        
    char *fullname, *filename, *saveptr, *next_filename, *prev_filename;
    fullname = trim_dir_path(name);
    
    if (strcmp(fullname, pathsep) == 0) {
        curr_dir = dir_open_root ();
        *local_name = malloc(2);
        strlcpy(*local_name, ".", 2);
        goto done;
    }
    
    filename = strtok_r(fullname, pathsep, &saveptr);
    
    if ( *name == (*pathsep) ) {
      curr_dir = dir_open_root ();
    }
    else {
      curr_dir = dir_open(inode_reopen(thread_current()->pwd));
      if (filename == NULL) filename = "";
    }
    
    /* search subdirectories */
    while(filename != NULL && curr_dir != NULL){
      if (!dir_lookup(curr_dir, filename, &dir_inode)) break;
      
      prev_filename = filename;
      prev_dir = curr_dir;
      if (inode_isdir(dir_inode))
        curr_dir = dir_open(dir_inode);
      else
        break;
      
      filename = strtok_r(NULL, pathsep, &saveptr);
      if (filename != NULL) {
          dir_close(prev_dir); prev_dir = NULL;
      }
    }
    
    next_filename = strtok_r(NULL, pathsep, &saveptr);
    if (next_filename != NULL || (create && dir_inode != NULL) ||  (!create && dir_inode == NULL)) {
        if (curr_dir != NULL) dir_close(curr_dir);
        if (dir_inode != NULL) inode_close(dir_inode);
        if (prev_dir != NULL && prev_dir != curr_dir) dir_close(prev_dir);
        curr_dir = NULL;
        goto done;
    }
        
    /* to allow to open directory as file */
    if (!create && filename == NULL) { /* file is actually directory */
        dir_close(curr_dir);
        curr_dir = prev_dir;
        filename = prev_filename;
    } else if (!create && dir_inode != NULL) {
        inode_close(dir_inode);
    }

    ASSERT(filename != NULL);
    *local_name = malloc(strlen(filename)+1);
    strlcpy(*local_name, filename, strlen(filename)+1);
    goto done;
        
    done:
      free(fullname);
      return curr_dir;
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
  char *filename = NULL;
  bool success = false;
  
  dir = parse_filepath(name, &filename, true);
  if (dir != NULL) success = ( free_map_allocate (1, &inode_sector, true)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
    
  dir_close (dir);
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
  struct dir *dir;
  char *filename = NULL;
  bool success = false;
      
  dir = parse_filepath(name, &filename, false);
    
  struct inode *inode = NULL;
  if (dir != NULL && filename != NULL)
    dir_lookup (dir, filename, &inode);
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
  struct dir *dir;
  char *filename = NULL;
  bool success = false;
    
  dir = parse_filepath(name, &filename, false);
  if (dir != NULL && filename != NULL) {
    dir_remove (dir, filename);
    success = true;
  }
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
