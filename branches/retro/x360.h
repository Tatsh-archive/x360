#ifndef __X360_H
#define __X360_H

#define FUSE_USE_VERSION 26
#define _GNU_SOURCE

#include <fuse.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

typedef struct {
    off_t start;
    off_t fat;
    off_t root_dir;
    size_t size;
    uint32_t fat_num;
    uint32_t *fat_ptr;
} x360_partition;

typedef struct {
    uint32_t magic;
    uint32_t vol_id;
    uint32_t cl_size;
    uint32_t fat_num;
    uint16_t zero;
}__attribute__((packed)) x360_boot_sector;

#define X360_FAT_MAX 0x6B0800
typedef uint32_t x360_fat[X360_FAT_MAX];

typedef struct {
    uint8_t fnsize;
    uint8_t attribute;
    int8_t filename[42];
    uint32_t cluster;
    uint32_t fsize;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t atime;
    uint16_t adate;
}__attribute__((packed)) x360_file_record;

// x360_fr.c
int x360_fr_create(x360_file_record *fr, void *data);
int x360_fr_mkdir(x360_file_record *fr, void *data);
int x360_fr_rename(x360_file_record *fr, void *data);
int x360_fr_truncate(x360_file_record *fr, void *data);
int x360_fr_unlink(x360_file_record *fr, void *data);

#define X360_DIR_MAX 256
typedef x360_file_record x360_dir_cluster[X360_DIR_MAX];

typedef int (*x360_function) (x360_file_record *fr, void *data);

#endif
