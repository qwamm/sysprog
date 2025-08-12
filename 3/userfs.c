#include "userfs.h"
#include <stddef.h>
#include <string.h>

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;
};

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 65536;

enum ufs_error_code ufs_errno()
{
	return ufs_error_code;
}

int create_file_descriptor(struct file *file_ptr)
{
        struct filedesc *new_file_desc = malloc(sizeof(struct filedesc));
        new_file_desc->file = file_ptr;
        file_descriptors = realloc(file_descriptors, sizeof(struct filedesc*)*(++file_descriptor_count));
        file_descriptors[file_descriptor_count-1] = file_ptr;
        file_ptr->refs++;
        return file_descriptor_count-1;
}

int ufs_open(const char *filename, int flags)
{
	ufs_error_code = UFS_ERR_NO_ERR;
	struct file *file_ptr = file_list;
	while (file_ptr != NULL)
	{
		if (strcmp(file_ptr->name, filename) == 0)
		{
			return create_file_descriptor(file_ptr);
		}
		file_ptr = file_ptr->next;
	}
	if (flags != UFS_CREATE) {
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	struct file *new_file = calloc(1, sizeof(struct file));
	int filename_len = strlen(filename);
	new_file->name = malloc(filename_len + 1);
	strcpy(new_file->name, filename, filename_len + 1);
	new_file->prev = file_list;
	file_list->next = new_file;
	file_list = file_list->next;
	return create_file_descriptor(new_file);
}

ssize_t ufs_write(int fd, const char *buf, size_t size)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)fd;
	(void)buf;
	(void)size;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

ssize_t ufs_read(int fd, char *buf, size_t size)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)fd;
	(void)buf;
	(void)size;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

int ufs_close(int fd)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)fd;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

int ufs_delete(const char *filename)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)filename;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

#if NEED_RESIZE

int ufs_resize(int fd, size_t new_size)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)fd;
	(void)new_size;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

#endif

void ufs_destroy(void) {}
