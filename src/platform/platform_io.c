/*------------------------------------------------------------------------*/
/*                                                                        *
 * platform_io.c — Platform-specific I/O wrapper implementations        *
 * WHDLoad Manager - Platform abstraction layer                          *
 *                                                                        *
 * Copyright © 2025 Kerry Thompson                                        *
 * SPDX-License-Identifier: MIT                                           *
 *                                                                        */
/*------------------------------------------------------------------------*/

#include <platform.h>
#include <platform/platform_io.h>


#ifdef PLATFORM_AMIGA
/* bring in all of the AmigaDOS types & prototypes */
#include <exec/types.h>          /* BOOL, APTR, ULONG, etc. */
#include <dos/dos.h>             /* Lock(), UnLock(), DeleteFile(), Rename(), SHARED_LOCK */
#include <dos/dostags.h>         /* SHARED_LOCK tag */
#include <dos/filehandler.h>     /* struct FileInfoBlock */
#include <dos/dosextens.h>       /* Examine(), ExNext() */
#include <proto/dos.h>           /* DOS library function prototypes */
#include <string.h>
#include <platform/platform_io.h>

/*------------------------------------------------------------------------*/
/* Directory scanning structures                                          */
/*------------------------------------------------------------------------*/

struct whd_dir {
    BPTR lock;                     /* Directory lock */
    struct FileInfoBlock *fib;     /* File info block */
    int first_call;                /* TRUE for first readdir call */
};

/*------------------------------------------------------------------------*/
/**
 * @brief Check file existence and access permissions
 *
 * @param path Path to the file to check
 * @param mode Access mode (F_OK for existence check)
 * @return int 0 on success, -1 on failure
 */
/*------------------------------------------------------------------------*/
int whd_access(const char *path, int mode)
{
	BPTR lock;

	/* Note: 'mode' is currently unused on Amiga; kept for API parity */

	lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
	if (lock)
	{
		UnLock(lock);
		return 0;  /* File exists */
	} /* if */

	return -1;  /* File doesn't exist or can't access */
} /* whd_access */

/*------------------------------------------------------------------------*/
/**
 * @brief Remove a file
 *
 * @param path Path to the file to remove
 * @return int 0 on success, -1 on failure
 */
/*------------------------------------------------------------------------*/
int whd_remove(const char *path)
{
	if (DeleteFile((CONST_STRPTR)path))
	{
		return 0;  /* Success */
	} /* if */

	return -1;  /* Failed */
} /* whd_remove */

/*------------------------------------------------------------------------*/
/**
 * @brief Rename a file
 *
 * @param oldpath Current path of the file
 * @param newpath New path for the file
 * @return int 0 on success, -1 on failure
 */
/*------------------------------------------------------------------------*/
int whd_rename(const char *oldpath, const char *newpath)
{
	if (Rename((CONST_STRPTR)oldpath, (CONST_STRPTR)newpath))
	{
		return 0;  /* Success */
	} /* if */

	return -1;  /* Failed */
} /* whd_rename */

/*------------------------------------------------------------------------*/
/**
 * @brief Open directory for scanning
 *
 * @param path Path to directory
 * @return whd_dir_t* Directory handle or NULL on error
 */
/*------------------------------------------------------------------------*/
whd_dir_t *whd_opendir(const char *path)
{
	struct whd_dir *dir;

	dir = (struct whd_dir *)whd_malloc(sizeof(struct whd_dir));
	if (!dir)
	{
		return NULL;
	} /* if */

	dir->fib = (struct FileInfoBlock *)whd_malloc(sizeof(struct FileInfoBlock));
	if (!dir->fib)
	{
		whd_free(dir);
		return NULL;
	} /* if */

	dir->lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
	if (!dir->lock)
	{
		whd_free(dir->fib);
		whd_free(dir);
		return NULL;
	} /* if */

	dir->first_call = 1;
	return (whd_dir_t *)dir;
} /* whd_opendir */

/*------------------------------------------------------------------------*/
/**
 * @brief Read next directory entry
 *
 * @param dir Directory handle
 * @param entry Pointer to entry structure to fill
 * @return int 1 on success, 0 on end of directory, -1 on error
 */
/*------------------------------------------------------------------------*/
int whd_readdir(whd_dir_t *dir_handle, whd_dir_entry_t *entry)
{
	struct whd_dir *dir = (struct whd_dir *)dir_handle;
	BOOL result;

	if (!dir || !entry)
	{
		return -1;
	} /* if */

	if (dir->first_call)
	{
		/* First call - examine the directory */
		result = Examine(dir->lock, dir->fib);
		dir->first_call = 0;
		if (!result)
		{
			return -1;  /* Error examining directory */
		} /* if */

		/* Skip the directory entry itself, call ExNext for first file */
		result = ExNext(dir->lock, dir->fib);
	}
	else
	{
		/* Subsequent calls - get next entry */
		result = ExNext(dir->lock, dir->fib);
	} /* if */

	if (!result)
	{
		return 0;  /* End of directory */
	} /* if */

	/* Fill entry structure */
	strncpy(entry->name, dir->fib->fib_FileName, sizeof(entry->name) - 1);
	entry->name[sizeof(entry->name) - 1] = '\0';
	entry->is_directory = (dir->fib->fib_DirEntryType > 0) ? 1 : 0;

	return 1;  /* Success */
} /* whd_readdir */

/*------------------------------------------------------------------------*/
/**
 * @brief Close directory handle
 *
 * @param dir Directory handle to close
 */
/*------------------------------------------------------------------------*/
void whd_closedir(whd_dir_t *dir_handle)
{
	struct whd_dir *dir = (struct whd_dir *)dir_handle;

	if (!dir)
	{
		return;
	} /* if */

	if (dir->lock)
	{
		UnLock(dir->lock);
	} /* if */

	if (dir->fib)
	{
		whd_free(dir->fib);
	} /* if */

	whd_free(dir);
} /* whd_closedir */

/*------------------------------------------------------------------------*/
/**
 * @brief Create a directory
 *
 * @param path Path to the directory to create
 * @return int 0 on success, -1 on failure
 */
/*------------------------------------------------------------------------*/
int whd_mkdir(const char *path)
{
	BPTR lock;

	lock = CreateDir((CONST_STRPTR)path);
	if (lock)
	{
		UnLock(lock);
		return 0;  /* Success */
	} /* if */

	/* Check if directory already exists */
	lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
	if (lock)
	{
		/* Directory already exists */
		UnLock(lock);
		return 0;  /* Success */
	} /* if */

	return -1;  /* Failed */
} /* whd_mkdir */

#else /* !PLATFORM_AMIGA */

/*------------------------------------------------------------------------*/
/* Host platform implementation                                           */
/*------------------------------------------------------------------------*/

#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

/*------------------------------------------------------------------------*/
/**
 * @brief Read next directory entry (host implementation)
 *
 * @param dir Directory handle (cast from DIR*)
 * @param entry Pointer to entry structure to fill
 * @return int 1 on success, 0 on end of directory, -1 on error
 */
/*------------------------------------------------------------------------*/
int whd_readdir(whd_dir_t *dir_handle, whd_dir_entry_t *entry)
{
	DIR *dir = (DIR *)dir_handle;
	struct dirent *ent;
	struct stat st;
	char full_path[512];

	if (!dir || !entry)
	{
		return -1;
	} /* if */

	ent = readdir(dir);
	if (!ent)
	{
		return 0;  /* End of directory */
	} /* if */

	/* Fill entry structure */
	strncpy(entry->name, ent->d_name, sizeof(entry->name) - 1);
	entry->name[sizeof(entry->name) - 1] = '\0';

	/* Determine if it's a directory by checking d_type or using stat */
#ifdef DT_DIR
	if (ent->d_type == DT_DIR)
	{
		entry->is_directory = 1;
	}
	else if (ent->d_type == DT_REG)
	{
		entry->is_directory = 0;
	}
	else
#endif
	{
		/* Use stat as fallback */
		snprintf(full_path, sizeof(full_path), "%s/%s", ".", ent->d_name);
		if (stat(full_path, &st) == 0)
		{
			entry->is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
		}
		else
		{
			entry->is_directory = 0;  /* Assume file on stat failure */
		} /* if */
	} /* if */

	return 1;  /* Success */
} /* whd_readdir */

#endif /* PLATFORM_AMIGA */

/*------------------------------------------------------------------------*/
/**
 * @brief Normalize path separators to platform-native format
 *
 * Converts path separators to the appropriate format for the current platform.
 * On Amiga: converts \ to /
 * On Windows: converts / to \
 * On Unix-like: converts \ to /
 *
 * @param path Path string to normalize (modified in place)
 */
/*------------------------------------------------------------------------*/
void whd_normalize_path(char *path)
{
	char *p;		/* Path pointer */

	if (!path) {
		return;
	} /* if */

	p = path;
	while (*p) {
#ifdef PLATFORM_AMIGA
		/* On Amiga, use forward slashes */
		if (*p == '\\') {
			*p = '/';
		} /* if */
#elif defined(_WIN32)
		/* On Windows, use backslashes */
		if (*p == '/') {
			*p = '\\';
		} /* if */
#else
		/* On Unix-like systems, use forward slashes */
		if (*p == '\\') {
			*p = '/';
		} /* if */
#endif
		p++;
	} /* while */
} /* whd_normalize_path */

/*------------------------------------------------------------------------*/
/* End of Text */
