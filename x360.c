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
#include <time.h>
#include <bits/byteswap.h>
#include <linux/types.h>

#include "x360.h"

static const char *slash = "/";
static uint32_t fatx_magic = 0x46415458;
static uid_t uid;
static gid_t gid;
static int32_t fd;
static x360_fat x360_table;
static x360_partition pt = {
    0x130EB0000ll,
    0, 0, 0, 0, NULL
};
static x360_boot_sector bs;

static inline time_t x360_fat2unix_time(uint16_t time_be, uint16_t date_be) {
    struct tm t;
    uint16_t time_le = __bswap_16(time_be);
    uint16_t date_le = __bswap_16(date_be);

    t.tm_year = ((date_le & 0xFE00) >> 9) + 80;
    t.tm_mon = ((date_le & 0x1E0) >> 5) - 1;
    t.tm_mday = (date_le & 0x1F);

    t.tm_hour = ((time_le & 0xF800) >> 11);
    t.tm_min = ((time_le & 0x7E0) >> 5);
    t.tm_sec = ((time_le & 0x1F) << 1);

    return mktime(&t);
}

static inline void x360_read_table() {
    lseek(fd, pt.fat, SEEK_SET);
    read(fd, pt.fat_ptr, pt.fat_num * sizeof(uint32_t));
}

// TODO: move functions to x360_partition.c
static inline void x360_partition_calc_size() {
    pt.fat = pt.start + 0x1000ll;
    off_t end = lseek(fd, 0, SEEK_END);
    pt.root_dir = -(-((end - pt.start)>>12) & -0x1000ll) + pt.fat;
    pt.size = end - pt.root_dir;
    pt.fat_num = pt.size >> 14;
    pt.fat_ptr = malloc(pt.fat_num * sizeof(uint32_t));
    x360_read_table();
}
// end TODO

static inline void x360_write_table() {
    lseek(fd, pt.fat, SEEK_SET);
    write(fd, pt.fat_ptr, pt.fat_num * sizeof(uint32_t));
}

static inline off_t x360_cluster_off_swap(uint32_t cluster) {
    return ((off_t)(__bswap_32(cluster) - 1) << 14ll) + pt.root_dir;
}

static inline off_t x360_cluster_off(uint32_t cluster) {
    return ((off_t)(cluster - 1) << 14ll) + pt.root_dir;
}

static off_t x360_cluster(x360_file_record *fr, uint32_t cluster) {
    uint32_t i, c = __bswap_32(fr->cluster);
    for (i = 0; i < cluster; i++)
        c = __bswap_32(pt.fat_ptr[c]);
    return c;
}

static off_t x360_offset_token(const char *path, off_t dir, x360_file_record *fr) {
    x360_dir_cluster c;
    int i;
    size_t len = strlen(path);
    if (lseek(fd, dir, SEEK_SET) != dir) return -ENOENT;
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

    if (strcmp(path, slash) == 0) {
        stbuf->st_mode = S_IFDIR|0555;
        stbuf->st_nlink = 2;
        return 0;
    }

    off_t start = x360_offset(path, pt.root_dir, &fr);
    if (start < 0) return (int) start;

    stbuf->st_mode = (fr.attribute & 0x10) ? (S_IFDIR|0555) : (S_IFREG|0444);
    stbuf->st_nlink = (fr.attribute & 0x10) ? 2 : 1;
    stbuf->st_size = (fr.attribute & 0x10) ? 0 : __bswap_32(fr.fsize);
    stbuf->st_mtime = x360_fat2unix_time(fr.mtime, fr.mdate);
    stbuf->st_ctime = x360_fat2unix_time(fr.ctime, fr.cdate);
    stbuf->st_atime = x360_fat2unix_time(fr.atime, fr.adate);
    stbuf->st_uid = uid;
    stbuf->st_gid = gid;

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
    if (lseek(fd, start, SEEK_SET) != start) return -ENOENT;
    read(fd, &c, sizeof (x360_dir_cluster));

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (i = 0; i < X360_DIR_MAX; i++) {
        if (c[i].fnsize == 0xFF) break;
        if (c[i].fnsize < 43) {
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
    uint32_t cl = offset >> 14;
    cl = x360_cluster(&fr, cl);
    start = x360_cluster_off(cl);
    offset %= 0x4000;
    start += offset;
    lseek(fd, start, SEEK_SET);

    size = (size > __bswap_32(fr.fsize)) ? __bswap_32(fr.fsize) : size;
    total = size;
    while (offset + size > 0x4000) {
        read(fd, buf, 0x4000 - offset);
        cl = __bswap_32(pt.fat_ptr[cl]);
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

static inline int usage(char *path) {
    fprintf(stderr, "Usage: %s [options] /dev/sdx /mount/point\n\n", basename(path));
    fputs("Options are:\n", stderr);
    fputs("-d        debug mode\n", stderr);
    fputs("-g gid    gid of mounted files\n", stderr);
    fputs("-h        show this message\n", stderr);
    fputs("-o offset offset to begining of FATX partition\n", stderr);
    fputs("-u uid    uid of mounted files\n\n", stderr);
    return -1;
}

int main(int argc, char *argv[]) {
    int32_t magic, c, fargc = 2;
    int32_t debug = 0;
    int32_t override = 0;
    struct stat st;
    uid = getuid();
    gid = getgid();
    while ((c = getopt(argc, argv, "dg:ho:u:")) != -1) {
        switch (c) {
            case 'd':
                debug = 1;
                break;
            case 'g':
                gid = atoi(optarg);
                break;
            case 'h':
                usage(argv[0]);
                break;
            case 'o':
                pt.start = atoi(optarg);
                override = 1;
                break;
            case 'u':
                uid = atoi(optarg);
                break;
        }
    }
    if (argc - optind < 2) return usage(argv[0]);
    fd = open(argv[optind], O_RDWR);
    if (fd < 1) {
        printf("Error opening %s: %s\n", argv[optind], strerror(errno));
        return -1;
    }
    fstat(fd, &st);
    if (!override && S_ISREG(st.st_mode))
        pt.start = 0;
    if (lseek(fd, pt.start, SEEK_SET) != pt.start) {
        printf("Error seeking %s\n", argv[optind]);
        close(fd);
        return -1;
    }
    read(fd, &magic, sizeof (int32_t));
    if (magic != fatx_magic) {
        printf("Error: %s is not a Xbox 360 hard drive\n", argv[optind]);
        close(fd);
        return -1;
    }
    x360_partition_calc_size();
    fargc += debug;
    char *fargv[4] = {argv[0], argv[optind + 1], debug ? "-d" : NULL, NULL};
    int ret = fuse_main(fargc, fargv, &x360_oper, NULL);
    close(fd);
    return ret;
}
