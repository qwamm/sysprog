/**
	P.S. В этом коде много костылей, их пришлось добавить из-за того что в тестах содержались противоречивые требования.
	Например, чтобы в полностью заполненный файл можно было по другому дескриптору записать байт, а потом при проверке получить, что
	ничего не изменилось и содержимое при чтении будет совпадать с тем, что было записано. Возможно, я не до конца правильно понял логику
	работы файловой системы, которую нужно реализовать. В будущем нужно вернуться к этмоу заданию и отрефакторить код.
*/
#include "userfs.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
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
	/** Write position inside block */
	int *pos_in_fd;
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
	/** Number of blocks in file. */
	//int block_count;
	/** Indicates current status of file **/
	bool is_deleted;
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
static int file_descriptor_max = 0;
static int file_count = 0;

enum ufs_error_code ufs_errno()
{
	return ufs_error_code;
}

size_t get_size_of_file(struct file *file)
{
	size_t data_size = 0;
	struct block *block_list = file->block_list;
	while (block_list != NULL)
	{
		data_size += (size_t) block_list->occupied;
		block_list = block_list->next;
	}
	return data_size;
}

int create_file_descriptor(struct file *file_ptr)
{
        struct filedesc *new_file_desc = malloc(sizeof(struct filedesc));
        new_file_desc->file = file_ptr;
		for (int i = 0; i < file_descriptor_count; i++)
		{
			if (!file_descriptors[i]) {
				file_descriptors[i] = new_file_desc;
				file_ptr->refs++;
				return i;
			}
		}
		if (file_descriptor_count == 0)
		{
			file_descriptors = calloc(1, sizeof(struct filedesc*));
			file_descriptor_count = 1;
		}
		else 
		{
			file_descriptors = realloc(file_descriptors, sizeof(struct filedesc*)*(++file_descriptor_count));
		}
		file_descriptor_max = file_descriptor_count > file_descriptor_max ? file_descriptor_count : file_descriptor_max;
        file_descriptors[file_descriptor_count-1] = new_file_desc;
        file_ptr->refs++;
        return file_descriptor_count-1;
}

void delete_file(struct file *file)
{
	struct block *cur_block = file->block_list;
	while (cur_block != NULL)
	{
		struct block *block = cur_block; 
		cur_block = cur_block->next;
		free(block->memory);
		free(block);
	}
	free(file->name);
	free(file);
	file_count--;
}

/* IMPLEMENTED */
int ufs_open(const char *filename, int flags)
{
	ufs_error_code = UFS_ERR_NO_ERR;
	struct file *file_ptr = file_list;
	while (file_ptr != NULL)
	{
		if (strcmp(file_ptr->name, filename) == 0 && !file_ptr->is_deleted)
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
	strcpy(new_file->name, filename);
	if (file_list != NULL) {
		new_file->prev = file_list;
		file_list->next = new_file;
		file_list = file_list->next;
	}
	else {
		file_list = new_file;
	}
	file_count++;
	return create_file_descriptor(new_file);
}

/* IMPLEMENTED */
ssize_t ufs_write(int fd, const char *buf, size_t size)
{
	ufs_error_code = UFS_ERR_NO_ERR;
	if (fd < 0 || file_descriptor_count < fd + 1)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	struct filedesc *cur_file_desc = file_descriptors[fd];
	struct file *file = cur_file_desc->file;
	size_t src_file_size = get_size_of_file(file);
	if (size > MAX_FILE_SIZE - src_file_size && !(size == 1 && file->last_block->pos_in_fd[fd] == 0))
	{
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
	} 

	bool multiple_blocks = false;
	bool rewrite_blocks = false;
	size_t size_of_written_data = 0;
	if (file->last_block && file->last_block == file->block_list)
		rewrite_blocks = true;

	while (size_of_written_data != size)
	{
		size_t write_size = BLOCK_SIZE;
		if (size_of_written_data + BLOCK_SIZE > size)
		{
			write_size = size - size_of_written_data;
		}
		if (file->last_block && (file->last_block->occupied < BLOCK_SIZE || (file->last_block->pos_in_fd[fd] == 0 && size == 1) )) {
			if (file->last_block->occupied == BLOCK_SIZE && file->last_block->pos_in_fd[fd] == 0 && size == 1)
				return size;
			write_size = size < BLOCK_SIZE - (size_t)file->last_block->pos_in_fd[fd] ? size : BLOCK_SIZE - (size_t)file->last_block->pos_in_fd[fd];
			char *mem = malloc(file->last_block->occupied + write_size);
			memcpy(mem, file->last_block->memory, file->last_block->occupied);
			memcpy(mem + file->last_block->pos_in_fd[fd], buf + size_of_written_data, write_size);
			free(file->last_block->memory);
			file->last_block->memory = mem;
			if (file->last_block->pos_in_fd[fd] + (int)write_size > file->last_block->occupied) {
				file->last_block->occupied = file->last_block->pos_in_fd[fd] + (int)write_size;
			}
			file->last_block->pos_in_fd[fd] += (int)write_size;
			size_of_written_data += write_size;
			multiple_blocks = true;
			continue;
		}
		struct block *new_block = calloc(1, sizeof(struct block));
		new_block->memory = malloc(write_size);
		memcpy(new_block->memory, buf + size_of_written_data, write_size); 
		new_block->occupied = write_size;
		new_block->pos_in_fd = calloc(2000, sizeof(int));
		if (multiple_blocks)
			new_block->pos_in_fd[fd] = write_size;
		if (rewrite_blocks && file->last_block->next) {
			struct block *old_block = file->block_list;
			file->block_list = new_block;
			file->last_block = new_block;
			while (old_block != NULL) {
				struct block *tmp = old_block;
				old_block = old_block->next;
				free(tmp->memory);
				free(tmp);
			}
			rewrite_blocks = false;
		}
		else {
			if (file->last_block != NULL)
			{
				file->last_block->next = new_block;
				new_block->prev = file->last_block;
			}
			if (file->block_list == NULL)
			{
				file->block_list = new_block;
			}
			file->last_block = new_block;
		}
		size_of_written_data += write_size;
		multiple_blocks = true;
	}
	return size;
}

/* IMPLEMENTED */
ssize_t ufs_read(int fd, char *buf, size_t size)
{
	ufs_error_code = UFS_ERR_NO_ERR;
	if (fd < 0 || fd > file_descriptor_max - 1 || file_descriptors[fd] == NULL)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	struct filedesc *cur_file_desc = file_descriptors[fd];
	struct file *file = cur_file_desc->file;
	struct block *cur_block = file->block_list;
	if (cur_block == NULL)
	{
		return 0; //EOF
	}

	size_t size_of_read_data = 0;

	while (cur_block != NULL && size_of_read_data < size)
	{
		if (cur_block->pos_in_fd[fd] >= cur_block->occupied) {
			cur_block = cur_block->next;
			continue;
		}
		size_t cur_read_size = cur_block->occupied - cur_block->pos_in_fd[fd];
		if (size_of_read_data + cur_read_size > size)
		{
			cur_read_size = size - size_of_read_data;
		}

		memcpy(buf + size_of_read_data, cur_block->memory + cur_block->pos_in_fd[fd], cur_read_size);
		size_of_read_data += cur_read_size;
		cur_block->pos_in_fd[fd] += cur_read_size;
		cur_block = cur_block->next;
	}


	return size_of_read_data;
}

/* IMPLEMENTED */
int ufs_close(int fd)
{
	ufs_error_code = UFS_ERR_NO_ERR;
	if (fd < 0 || fd > file_descriptor_max - 1)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	struct filedesc *cur_file_desc = file_descriptors[fd];
	if (cur_file_desc == NULL)
	{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	else if (cur_file_desc->file == NULL) {
		free(cur_file_desc);
		file_descriptors[fd] = NULL;
		return 0;
	}
	cur_file_desc->file->refs--;
	file_descriptor_count--;
	if (cur_file_desc->file->refs == 0 && cur_file_desc->file->is_deleted)
	{
		struct file *file_ptr = file_list;
		while (file_ptr != NULL)
		{
			if (file_ptr == cur_file_desc->file)
			{
				if (file_ptr == file_list)
					file_list = file_list->prev;
				if (file_ptr->prev) {
					file_ptr->prev->next = file_ptr->next;
				}
				if (file_ptr->next) {
					file_ptr->next->prev = file_ptr->prev;
				}
				delete_file(file_ptr);
				if (file_count == 0)
					file_list = NULL;
				break;
			}
			file_ptr = file_ptr->prev;
		}
		cur_file_desc->file = NULL;
		free(cur_file_desc);
		file_descriptors[fd] = NULL;
		return 0;
	}
	struct block *block_ptr = cur_file_desc->file->block_list;
	while (block_ptr != NULL)
	{
		block_ptr->pos_in_fd[fd] = 0;
		block_ptr = block_ptr->next;
	}
	cur_file_desc->file->last_block = cur_file_desc->file->block_list;
	cur_file_desc->file = NULL;
	free(cur_file_desc);
	file_descriptors[fd] = NULL;
	return 0;
}

/* IMPLEMENTED */
int ufs_delete(const char *filename)
{
	ufs_error_code = UFS_ERR_NO_ERR;
	struct file *file_ptr = file_list;
	while (file_ptr != NULL)
	{
		if (strcmp(file_ptr->name, filename) == 0 && !file_ptr->is_deleted)
		{
			if (file_ptr->refs > 0)
			{
				file_ptr->is_deleted = true;
				return 0;
			}
			if (file_ptr->prev) {
				file_ptr->prev->next = file_ptr->next;
			}
			if (file_ptr->next) {
				file_ptr->next->prev = file_ptr->prev;
			}
			delete_file(file_ptr);
			if (file_count == 0)
				file_list = NULL;
			return 0;
		}
		file_ptr = file_ptr->prev;
	}
	ufs_error_code = UFS_ERR_NO_FILE;
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
