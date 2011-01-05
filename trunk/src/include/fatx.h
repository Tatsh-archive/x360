/*
 * libfatx.h
 *
 *  Created on: Dec 15, 2010
 *      Author: ice
 */

#ifndef FATX_H_
#define FATX_H_

#include <stdio.h>
#include <stddef.h>
#include <time.h>

typedef struct fatx_fs_info fatx_fs_info;

typedef struct fatx_file_record {
	char name[43];
	size_t size;
	time_t modified;
	time_t created;
	time_t accessed;
	int isdir;
} fatx_file_record;

typedef struct fatx_file_offsets {
	off_t record_offset;
	off_t data_offset;
} fatx_file_offsets;

fatx_fs_info *fatx_fs_init(const char *filename);
void fatx_fs_end(fatx_fs_info *info);
int fatx_find_file_offsets(struct fatx_file_offsets *offsets,
		fatx_fs_info *info, const char *path);
int fatx_read_file_record(fatx_file_record *file_record,
		fatx_fs_info *info, const char *path);
int fatx_list_dir(fatx_fs_info *info, const char *path, void (*func)(const char *, void *), void *user);

#endif /* FATX_H_ */
