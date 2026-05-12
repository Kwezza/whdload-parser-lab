#ifndef PLATFORM_IO_H
#define PLATFORM_IO_H

#include <stdio.h>                 /* always available */

#if !PLATFORM_AMIGA                /* host-only extras */
  #include <sys/stat.h>
  #include <dirent.h>
  #include <errno.h>
#endif

/* File I/O wrappers - portable across platforms */
#define whd_fopen   fopen
#define whd_fclose  fclose
#define whd_fread   fread
#define whd_fwrite  fwrite
#define whd_fseek   fseek
#define whd_ftell   ftell
#define whd_rewind  rewind

/* File existence and status */
#ifdef PLATFORM_AMIGA
  /* On Amiga, implement using dos.library calls in platform_io.c */
  int whd_access(const char *path, int mode);
  int whd_remove(const char *path);
  int whd_rename(const char *oldpath, const char *newpath);
  int whd_mkdir(const char *path);
#else
  /* On host, use standard POSIX calls */
  #include <unistd.h>
  #define whd_access  access
  #define whd_remove  remove
  #define whd_rename  rename

  #ifdef _WIN32
    #include <direct.h>
    #define whd_mkdir(path) _mkdir(path)
  #else
    #include <sys/stat.h>
    #define whd_mkdir(path) mkdir(path, 0755)
  #endif
#endif

/* Directory scanning abstraction */
typedef struct whd_dir_entry {
    char name[256];        /* Entry name */
    int is_directory;      /* 1 if directory, 0 if file */
} whd_dir_entry_t;

typedef struct whd_dir whd_dir_t;

#ifdef PLATFORM_AMIGA
  /* Amiga directory scanning */
  whd_dir_t *whd_opendir(const char *path);
  int whd_readdir(whd_dir_t *dir, whd_dir_entry_t *entry);
  void whd_closedir(whd_dir_t *dir);
#else
  /* Host directory scanning */
  #define whd_opendir(path)       ((whd_dir_t*)opendir(path))
  int whd_readdir(whd_dir_t *dir, whd_dir_entry_t *entry);
  #define whd_closedir(dir)       closedir((DIR*)dir)
#endif

/* Console output abstraction */
/* NOTE: Both platforms now use standard printf() */

/* whd_normalize_path: declaration removed in Phase 2 header hygiene
 * (2026-05-11). Definition retained in platform_io.c. */

/* Standard constants */
#ifndef F_OK
#define F_OK 0  /* Test for existence */
#endif

#endif /* PLATFORM_IO_H */
