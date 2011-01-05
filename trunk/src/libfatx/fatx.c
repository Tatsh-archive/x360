/*
  libfatx: Userspace access to a FATX filesystem
  Copyright (C) 2010  Isaac Tepper <Isaac356@live.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <fatx.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <libgen.h>
#include <endian.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

#define FATX_MAGIC 0x46415458
#define max(a, b) (((a) > (b)) ? (a) : (b))

const char *delimiter = "/";

struct fatx_fs_info {
	int fd;
	int endianness;
	size_t width;
	int mode;
	off_t fat_offset;
	size_t fat_size;
	off_t end;
	size_t size;
	off_t root_dir;
};

struct fatx_internal_file_record {
	uint8_t name_length;
	uint8_t attributes;
	uint8_t name[42];
	uint32_t first_cluster;
	uint32_t size;
	uint32_t modified_time;
	uint32_t created_time;
	uint32_t accessed_time;
}__attribute__((packed));

#define fatx_warn_corruption(fmt, ...) fprintf(stderr, "libfatx: Warning: Possible filesystem corruption:\n" fmt "\n(file: %s, line: %d)\n", __VA_ARGS__, __FILE__, __LINE__)

/**
 * Converts a name from a FATX record into ansi format (null-terminated).
 * A FATX name cannot be more than 42 characters, which means an ansi_name
 * allocated to 43 characters will fit any FATX name.
 */
static void fatx_name_fatx2ansi(char *ansi_name, uint8_t *fatx_name, int length) {
	int i;
	if (length > 42) length = 42;
	for (i = 0; i < length; i++) {
		ansi_name[i] = fatx_name[i];
	}
	ansi_name[length] = 0;
}

/**
 * Converts a name from ansi format into a FATX record (followed by 42-len FF bytes)
 * A FATX name cannot be more than 42 characters, which means that excess characters
 * in ansi_name will be dropped. The length field in a FATX file record will need to
 * be updated.
 */
static void fatx_name_ansi2fatx(uint8_t *fatx_name, char *ansi_name, int *length) {
	int i;
	*length = strlen(ansi_name);
	if ((unsigned int)(*length) > 42) *length = 42;
	for (i = 0; i < *length; i++) {
		fatx_name[i] = ansi_name[i];
	}
	for (; i < 42; i++) {
		fatx_name[i] = 0xFF;
	}
}

static time_t fatx_time_fatx2unix(uint32_t time) {
	struct tm t;
	t.tm_year = (time >> 25) + 80;
	t.tm_mon = ((time >> 21) & 0xF) - 1;
	t.tm_mday = (time >> 16) & 0x1F;
	t.tm_hour = (time >> 11) & 0x1F;
	t.tm_min = (time >> 5) & 0x3F;
	t.tm_sec = time & 0x1F;
	return mktime(&t);
}

static uint32_t fatx_time_unix2fatx(time_t time) {
	uint32_t ret;
	struct tm t;
	ret = 0;
	if (localtime_r(&time, &t) != &t) {
		fprintf(stderr, "libfatx: Warning: localtime_r could not convert the time.");
		return -1;
	}
	ret |= (t.tm_year - 80) << 25;
	ret |= ((t.tm_mon + 1) & 0xF) << 21;
	ret |= (t.tm_mday & 0x1F) << 16;
	ret |= (t.tm_hour & 0x1F) << 11;
	ret |= (t.tm_min & 0x3F) << 5;
	ret |= t.tm_sec & 0x1F;
	return ret;
}

static inline uint32_t fatx_read_int(fatx_fs_info *info, off_t offset);
static inline uint16_t fatx_read_short(fatx_fs_info *info, off_t offset);

static inline off_t fatx_next_cluster_offset(fatx_fs_info *info, off_t offset);

int fatx_find_file_offsets(fatx_file_offsets *offsets,
		fatx_fs_info *info, const char *path) {
	char *d_path, *save_ptr, *token, name[43];
	int i;
	off_t record_offset, data_offset;
	struct fatx_internal_file_record records[256];
	if (!strcmp(delimiter, path)) { // root directory, has no record
		offsets->record_offset = -1;
		offsets->data_offset = info->root_dir;
		return 0;
	}
	d_path = strdupa(path);
	data_offset = info->root_dir;
	token = strtok_r(d_path, delimiter, &save_ptr);
	if (token == NULL) return -1;
	do {
		while (1) {
			size_t read = pread(info->fd, records, sizeof(records), data_offset);
			for (i = 0; i < 256; i++) {
				if (records[i].name_length == 0xFF) {
					return -1;
				} else if (records[i].name_length == 0xE5) { // deleted file, skip
					continue;
				} else if (records[i].name_length > 42) {
					fatx_warn_corruption("Filename length is an invalid value (possible that we stepped into a file somehow)\nname_length: %d", records[i].name_length);
					return -1;
				}
				fatx_name_fatx2ansi(name, records[i].name, records[i].name_length);
				if ((strlen(token) == records[i].name_length) && (!strcasecmp(token, name))) {
					record_offset = (off_t) (sizeof(struct fatx_internal_file_record) * i) + data_offset;
					if (info->endianness == BIG_ENDIAN) data_offset = be32toh(records[i].first_cluster) - 1;
					if (info->endianness == LITTLE_ENDIAN) data_offset = le32toh(records[i].first_cluster) - 1;
					data_offset <<= 14;
					data_offset += info->root_dir;
					break;
				}
			}
			if ((strlen(token) == records[i].name_length) && (!strcasecmp(token, name))) {
				break;
			}
			data_offset = fatx_next_cluster_offset(info, data_offset);
			if (data_offset < 0) return -1;
		}
	} while ((token = strtok_r(NULL, delimiter, &save_ptr)) != NULL);
	offsets->record_offset = record_offset;
	offsets->data_offset = data_offset; // you can see that there's problems if sizeof(off_t) < 64 bits
	return 0;
}

int fatx_read_file_record(fatx_file_record *file_record,
		fatx_fs_info *info, const char *path) {
	fatx_file_offsets offsets;
	memset(file_record, 0, sizeof(fatx_file_record));
	int ret = fatx_find_file_offsets(&offsets, info, path);
	if (ret < 0) return ret;
	struct fatx_internal_file_record internal_file_record;
	if (offsets.record_offset == -1 && offsets.data_offset == info->root_dir) {
		file_record->name[0] = '/';
		file_record->name[1] = 0;
		file_record->isdir = 1;
		return 0;
	}
	size_t read = pread(info->fd, &internal_file_record,
			sizeof(struct fatx_internal_file_record), offsets.record_offset);
	if (read != sizeof(struct fatx_internal_file_record)) { return -1; }
	fatx_name_fatx2ansi(file_record->name, internal_file_record.name,
			internal_file_record.name_length);
	if (internal_file_record.attributes & 0x10) {
		file_record->isdir = 1;
	} else {
		file_record->isdir = 0;
	}
	if (info->endianness == LITTLE_ENDIAN) {
		file_record->size = (size_t) le32toh(internal_file_record.size);
		file_record->modified = fatx_time_fatx2unix(le32toh(internal_file_record.modified_time));
		file_record->created = fatx_time_fatx2unix(le32toh(internal_file_record.created_time));
		file_record->accessed = fatx_time_fatx2unix(le32toh(internal_file_record.accessed_time));
	} else if (info->endianness == BIG_ENDIAN) {
		file_record->size = (size_t) be32toh(internal_file_record.size);
		file_record->modified = fatx_time_fatx2unix(be32toh(internal_file_record.modified_time));
		file_record->created = fatx_time_fatx2unix(be32toh(internal_file_record.created_time));
		file_record->accessed = fatx_time_fatx2unix(be32toh(internal_file_record.accessed_time));
	} else {
		return -1;
	}
	return 0;
}

/**
 * Reads a short from a given offset without moving the file marker.
 */
static inline uint16_t fatx_read_short_fd(int fd, off_t offset) {
	uint16_t ret;
	size_t read = pread(fd, &ret, sizeof(uint16_t), offset);
	return ret;
}

/**
 * Reads a short from a given offset without moving the file marker.
 * Takes into account the endianness of the filesystem.
 */
static inline uint16_t fatx_read_short(fatx_fs_info *info, off_t offset) {
	uint16_t ret;
	size_t read = pread(info->fd, &ret, sizeof(uint16_t), offset);
	if (info->endianness == BIG_ENDIAN) ret = be32toh(ret);
	if (info->endianness == LITTLE_ENDIAN) ret = le32toh(ret);
	return ret;
}

/**
 * Reads an integer from a given offset without moving the file marker.
 */
static inline uint32_t fatx_read_int_fd(int fd, off_t offset) {
	uint32_t ret;
	size_t read = pread(fd, &ret, sizeof(uint32_t), offset);
	return ret;
}

/**
 * Reads an integer from a given offset without moving the file marker.
 * Takes into account the endianness of the filesystem.
 */
static inline uint32_t fatx_read_int(fatx_fs_info *info, off_t offset) {
	uint32_t ret;
	size_t read = pread(info->fd, &ret, sizeof(uint32_t), offset);
	if (info->endianness == BIG_ENDIAN) ret = be32toh(ret);
	if (info->endianness == LITTLE_ENDIAN) ret = le32toh(ret);
	return ret;
}

/**
 * FATX doesn't have any information in the header realating to the size of the
 * filesystem or the location of the root directory. Instead, this information is
 * calculated based off of the size of the partition/drive.
 */
static inline void fatx_calc_size_and_table_offset(fatx_fs_info *info) {
	off_t here = lseek(info->fd, 0, SEEK_CUR);
	info->fat_offset = (off_t) 0x1000;
	info->end = lseek(info->fd, 0, SEEK_END);
	info->width = (info->end < 0x3FFF4000) ? sizeof(uint16_t) : sizeof(uint32_t);
	if (info->width == sizeof(uint32_t)) {
		info->root_dir = -(-((info->end >> 12) + 1) & INT64_C(-0x1000)) + info->fat_offset;
	} else if (info->width == sizeof(uint16_t)) {
		info->root_dir = -(-((info->end >> 13) + 1) & INT64_C(-0x1000)) + info->fat_offset;
	}
	info->size = info->end - info->root_dir;
	info->fat_size = info->size >> 14;
	lseek(info->fd, here, SEEK_SET);
}

/**
 * Find endianness of the filesystem pointed to by filename.
 * Returns LITTLE_ENDIAN for little endian, BIG_ENDIAN for big endian,
 * or -1 if it is not a fatx partition. (BIG_ENDIAN and LITTLE_ENDIAN
 * are defined in endian.h)
 */
static inline int fatx_find_endianness(int fd) {
	uint32_t magic = fatx_read_int_fd(fd, 0);
	/*
	 * Little endian partitions have FATX as their magic identifier.
	 * Since the code was never changed, big endian partitions have XTAF.
	 * Note that this is backwards from what it seems it should be.
	 */
	if (be32toh(magic) == FATX_MAGIC) { // Ascii: F A T X
		return LITTLE_ENDIAN;
	} else if (le32toh(magic) == FATX_MAGIC) { // Ascii: X T A F
		return BIG_ENDIAN;
	}
	return -1;
}

static uint32_t fatx_next_cluster(fatx_fs_info *info, uint32_t cluster);

static inline off_t fatx_next_cluster_offset(fatx_fs_info *info, off_t offset) {
	off_t ret;
	ret = (off_t)(fatx_next_cluster(info, ((offset - info->root_dir) >> 14) + 1));
	if (ret == 0xFFFFFFFF) return -1;
	if (ret == 0xFFFFFFFE) return -2;
	return ((off_t)(ret - 1) << 14) + info->root_dir;
}

static uint32_t fatx_next_cluster(fatx_fs_info *info, uint32_t cluster) {
	if (cluster == 1) return -1; // root directory can only be one cluster
	switch(info->width) {
	case sizeof(uint32_t):
		if ((cluster & 0xFFFFFFF) > info->fat_size && (cluster & 0xFFFFFFF) <  0xFFFFFF5) {
			fatx_warn_corruption("Current cluster is out of bounds\ncurrent_cluster: %d", cluster);
			return -1;
		}
		if ((cluster & 0xFFFFFFF) > 0xFFFFFF5) {
			fprintf(stderr, "libfatx: Warning: fatx_next_cluster was given an invalid cluster.\n");
			return -1;
		}
		cluster = fatx_read_int(info, info->fat_offset + cluster * sizeof(uint32_t));
		if ((cluster & 0xFFFFFFF) > info->fat_size && (cluster & 0xFFFFFFF) <  0xFFFFFF5) {
			fatx_warn_corruption("Current cluster is out of bounds\ncurrent_cluster: %d", cluster);
			return -1;
		}
		if ((cluster & 0xFFFFFFF) > 0xFFFFFF5) { // last cluster
			return -2;
		}
		return cluster;
	case sizeof(uint16_t):
		if (cluster > info->fat_size && cluster <  0xFFF5) {
			fatx_warn_corruption("Current cluster is out of bounds\ncurrent_cluster: %d", cluster);
			return -1;
		}
		if (cluster > 0xFFF5) {
			fprintf(stderr, "libfatx: Warning: fatx_next_cluster was given an invalid cluster.\n");
			return -1;
		}
		cluster = fatx_read_int(info, info->fat_offset + cluster * sizeof(uint32_t));
		if (cluster > info->fat_size && cluster <  0xFFF5) {
			fatx_warn_corruption("Current cluster is out of bounds\ncurrent_cluster: %d", cluster);
			return -1;
		}
		if (cluster > 0xFFF5) { // last cluster
			return -1;
		}
		return cluster;
	}
}

/**
 * Opens the file pointed to by filename. Returns a pointer to data
 * for the other fatx functions to use.
 */
fatx_fs_info *fatx_fs_init(const char *filename) {
	int fd, endianness;
	fatx_fs_info *info;
	info = malloc(sizeof(fatx_fs_info));
	if (info == NULL) {
		fputs("libfatx: fatal: Out of memory\n", stderr);
		return NULL;
	}
	fd = open(filename, O_RDWR);
	info->mode = O_RDWR;
	if (fd < 0) {
		if (errno == EACCES) { // permission denied; let's try read only
			fd = open(filename, O_RDONLY);
			if (fd < 0) {
				fprintf(stderr, "libfatx: Error opening file %s: [%d] %s\n",
						filename, errno, strerror(errno));
				return NULL;
			} else {
				fprintf(stderr, "libfatx: Warning: Opened file %s in read-only mode\n",
						filename);
				info->mode = O_RDONLY;
			}
		} else {
			fprintf(stderr, "libfatx: Error opening file %s: [%d] %s\n", filename,
					errno, strerror(errno));
			return NULL;
		}
	}
	info->fd = fd;
	endianness = fatx_find_endianness(fd);
	if (endianness < 0) {
		fprintf(stderr, "libfatx: Error: %s is not a FATX filesystem\n",
				filename);
		return NULL;
	}
	info->endianness = endianness;
	fatx_calc_size_and_table_offset(info);
	return info;
}

/**
 * Calls func on each file in the (sub)directory
 * Think of it as an "ls" type function
 */
int fatx_list_dir(fatx_fs_info *info, const char *path, void (*func)(const char *, void *), void *user) {
	off_t data_offset;
	fatx_file_offsets offsets;
	struct fatx_internal_file_record ifr;
	int ret = fatx_find_file_offsets(&offsets, info, path);
	if (ret < 0) return ret;
	if (offsets.record_offset > 0 || offsets.data_offset != info->root_dir) { // to avoid ENOTDIR on root directory
		pread(info->fd, &ifr, sizeof(ifr), offsets.record_offset);
		if ((ifr.attributes & 0x10) != 0x10) return -ENOTDIR;
	}
	data_offset = offsets.data_offset;
	while (1) {
		struct fatx_internal_file_record records[256];
		size_t read = pread(info->fd, records, sizeof(records), data_offset);
		int i;
		for (i = 0; i < 256; i++) {
			if (records[i].name_length == 0xFF) {
				return 0;
			} else if (records[i].name_length == 0xE5) { // deleted file, skip
				continue;
			} else if (records[i].name_length > 42) {
				fatx_warn_corruption("Filename length is an invalid value (possible that we stepped into a file somehow)\nname_length: %d", records[i].name_length);
				return -1;
			}
			char name[43];
			fatx_name_fatx2ansi(name, records[i].name, records[i].name_length);
			func(name, user);
		}
		data_offset = fatx_next_cluster_offset(info, data_offset);
		if (data_offset == -2) return 0;
		else if (data_offset < 0) return -1;
	}
	return 0;
}

/**
 * Reads size bytes from a file starting at offset. Expects buffer to be allocated for at least size bytes.
 */
size_t fatx_read_file(fatx_fs_info *info, const char *path, void *buffer, size_t size, off_t offset) {
	int i;
	uint32_t cluster;
	off_t data_offset;
	size_t read;
	fatx_file_offsets offsets;
	struct fatx_internal_file_record ifr;
	int ret = fatx_find_file_offsets(&offsets, info, path);
	if (ret < 0) return ret;
	if (offsets.record_offset < 0 && offsets.data_offset == info->root_dir) { // to avoid ENOTDIR on root directory
		return EISDIR;
	}
	pread(info->fd, &ifr, sizeof(struct fatx_internal_file_record), offsets.record_offset);
	if (ifr.attributes & 0x10) return EISDIR;
	switch(info->endianness) {
	case BIG_ENDIAN:
		cluster = be32toh(ifr.first_cluster);
		break;
	case LITTLE_ENDIAN:
		cluster = le32toh(ifr.first_cluster);
		break;
	default:
		return -1;
	}
	data_offset = offsets.data_offset;
	for (i = 0; i < (offset >> 14); i++) {
		data_offset = fatx_next_cluster_offset(info, data_offset);
		if (data_offset < 0) return -1;
	}
	offset -= (off_t)(i) << 14;
	read = 0;
	for (i = 0; i < ((offset + size) >> 14); i++) {
		read += pread(info->fd, (uint8_t *)(buffer) + read, 0x4000 - offset, data_offset + offset);
		offset = 0;
		data_offset = fatx_next_cluster_offset(info, data_offset);
		if (data_offset < 0) return -1;
	}
	size -= (off_t)(i) << 14;
	read += pread(info->fd, (uint8_t *)(buffer) + read, size, data_offset + offset);
	return read;
}

void fatx_fs_end(fatx_fs_info *info) {
	close(info->fd);
	free(info);
}
