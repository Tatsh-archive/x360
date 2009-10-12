/*
  x360: An Xbox 360 FUSE filesystem driver
  Copyright (C) 2009  Isaac Tepper <Isaac356@live.com>

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

#define FUSE_USE_VERSION 26
#define _GNU_SOURCE

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <bits/byteswap.h>
#include <linux/types.h>

#include "x360.h"

static uint32_t fatx_magic = 0x46415458;
static int32_t fd;
static x360_fat x360_table;
static x360_partition pt = {
    0x130EB0000ll,
    0x130EB1000ll,
    0x132973000ll,
};
static x360_boot_sector bs;

static inline void x360_read_table() {
    lseek(fd, pt.fat, SEEK_SET);
    read(fd, x360_table, sizeof(x360_fat));
}

static inline void x360_write_table() {
    lseek(fd, pt.fat, SEEK_SET);
    write(fd, x360_table, sizeof(x360_fat));
}

static inline off_t x360_cluster_off_swap(uint32_t cluster) {
    return ((__bswap_32(cluster) - 1) * 0x4000ll) + pt.root_dir;
}

static inline off_t x360_cluster_off(uint32_t cluster) {
    return ((cluster - 1) * 0x4000ll) + pt.root_dir;
}

static off_t x360_cluster(x360_file_record *fr, uint32_t cluster) {
    uint32_t i, c = __bswap_32(fr->cluster);
    for (i = 0; i < cluster; i++)
        c = __bswap_32(x360_table[c]);
    return c;
}

static off_t x360_offset_token(const char *path, off_t dir, x360_file_record *fr) {
    x360_dir_cluster c;
    int i;
    size_t len = strlen(path);
    if (lseek(fd, dir, SEEK_SET) < dir) return -ENOENT;
    read(fd, &c, sizeof(x360_dir_cluster));
    for (i = 0; i < X360_DIR_MAX; i++) {
        if (strncmp(c[i].filename, path, (len > c[i].fnsize ? len : c[i].fnsize)) == 0) {
            memcpy(fr, c + i, sizeof(x360_file_record));
            return x360_cluster_off_swap(c[i].cluster);
        }
    }
    return -ENOENT;
}

static off_t x360_offset(const char *path, off_t root_dir, x360_file_record *fr) {
    char *wpath = strdupa(path);
    off_t ret = root_dir;
    char *token = strtok(wpath, slash);
    while (token != NULL) {
        ret = x360_offset_token(token, ret, fr);
        if (ret < 0) return ret;
        token = strtok(NULL, slash);
    }
    return ret;
}

static int x360_getattr(const char *path, struct stat *stbuf) {
    x360_file_record fr;

    memset(stbuf, 0, sizeof (struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR|0555;
        stbuf->st_nlink = 2;
        return 0;
    }

    off_t start = x360_offset(path, pt.root_dir, &fr);
    if (start < 0) return (int) start;

    stbuf->st_mode = (fr.attribute & 0x10) ? (S_IFDIR|0555) : (S_IFREG|0444);
    stbuf->st_nlink = (fr.attribute & 0x10) ? 2 : 1;
    stbuf->st_size = (fr.attribute & 0x10) ? 0 : __bswap_32(fr.fsize);

    return 0;
}

static int x360_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;
    char filename[43];
    int i;
    x360_file_record fr;
    x360_dir_cluster c;

    off_t start = x360_offset(path, pt.root_dir, &fr);
    if (start < 0) return (int) start;
    lseek(fd, start, SEEK_SET);
    read(fd, &c, sizeof (x360_dir_cluster));

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (i = 0; i < X360_DIR_MAX; i++) {
        if (c[i].fnsize == 0xFF) break;
        if (c[i].fnsize != 0xE5) {
            memset(filename, 0, 43);
            memcpy(filename, c[i].filename, c[i].fnsize);
            filler(buf, filename, NULL, 0);
        }
    }

    //TODO: add support for multi-cluster directories. You're gonna need it.

    return 0;
}

static int x360_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    x360_file_record fr;
    size_t total;

    off_t start = x360_offset(path, pt.root_dir, &fr);
    uint32_t cl = offset / 0x4000;
    cl = x360_cluster(&fr, cl);
    start = x360_cluster_off(cl);
    offset %= 0x4000;
    start += offset;
    lseek(fd, start, SEEK_SET);

    size = (size > __bswap_32(fr.fsize)) ? __bswap_32(fr.fsize) : size;
    total = size;
    while (offset + size > 0x4000) {
        read(fd, buf, 0x4000 - offset);
        cl = __bswap_32(x360_table[cl]);
        size -= (0x4000 - offset);
        buf += (0x4000 - offset);
        lseek(fd, x360_cluster_off(cl), SEEK_SET);
        offset = 0;
    }
    read(fd, buf, size);
    return total;
}

static struct fuse_operations x360_oper = {
    .getattr = x360_getattr,
    .readdir = x360_readdir,
    .read = x360_read,
};

static inline int usage() {
    puts("Usage: x360 /dev/sdx /mount/point");
    return -1;
}

int main(int argc, char *argv[]) {
    int32_t magic;
    if (argc < 3) return usage();
    fd = open(argv[1], O_RDWR);
    if (fd < 1) {
        printf("Error opening %s: %s\n", argv[1], strerror(errno));
        return -1;
    }
    if (lseek(fd, pt.start, SEEK_SET) != pt.start) {
        printf("Error seeking %s\n", argv[1]);
        close(fd);
        return -1;
    }
    read(fd, &magic, sizeof (int32_t));
    if (magic != fatx_magic) {
        printf("Error: %s is not a Xbox 360 hard drive\n", argv[1]);
        close(fd);
        return -1;
    }
    x360_read_table();
    char *fargv[4] = {argv[0], argv[2], "-d", NULL};
    int ret = fuse_main(3, fargv, &x360_oper, NULL);
    close(fd);
    return ret;
}
