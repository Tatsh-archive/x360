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

#include "x360.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <libgen.h>
#include <bits/byteswap.h>
#include <sys/mman.h>

#define OPTIONS \
OPTION(uid)\
OPTION(gid)\
OPTION(mode)\
OPTION(off)\
OPTION(debug)\

enum {
    #define OPTION(x) option_##x,
    OPTIONS
    #undef OPTION
};

int option(char *string) {
    #define OPTION(x) if (strcmp(string, #x) == 0) return option_##x;
    OPTIONS
    #undef OPTION
}

static const char *slash = "/";
static const char *optseparators = ",=";
static uint32_t fatx_magic = 0x46415458;
static uid_t uid;
static gid_t gid;
static int32_t fd;
static x360_fat x360_table;
x360_partition pt = {
    0x130EB0000ll,
    0, 0, 0, 0, NULL
};
static x360_boot_sector bs;

uint32_t x360_find_free_cluster() {
    uint32_t i;
    for (i = 0; i < pt.fat_num; i++)
        if ((__bswap_32(pt.fat_ptr[i]) & 0xFFFFFFF) == 0) return i;
    return 0;
}

time_t x360_fat2unix_time(uint16_t time_be, uint16_t date_be) {
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

void x360_partition_calc_size() {
    pt.fat = pt.start + 0x1000ll;
    off_t end = lseek(fd, 0, SEEK_END);
    pt.root_dir = -(-((end - pt.start) >> 12) & -0x1000ll) + pt.fat;
    pt.size = end - pt.root_dir;
    pt.fat_num = pt.size >> 14;
    pt.fat_ptr = mmap(NULL, pt.fat_num * sizeof(uint32_t), PROT_READ|PROT_WRITE, MAP_SHARED, fd, pt.fat);
    if (pt.fat_ptr <= 0) {
        printf("Error mapping FAT table: %s\n", strerror(errno));
        close(fd);
        exit(1);
    }
}

off_t x360_cluster_off_swap(uint32_t cluster) {
    return ((off_t) (__bswap_32(cluster) - 1) << 14ll) + pt.root_dir;
}

off_t x360_cluster_off(uint32_t cluster) {
    return ((off_t) (cluster - 1) << 14ll) + pt.root_dir;
}

uint32_t x360_cluster(x360_file_record *fr, uint32_t cluster) {
    uint32_t i, c = __bswap_32(fr->cluster);
    for (i = 0; i < cluster; i++)
        c = __bswap_32(pt.fat_ptr[c]);
    return c;
}

uint32_t x360_clusters(x360_file_record *fr) {
    uint32_t i, c = __bswap_32(fr->cluster);
    for (i = 0; (c & 0xFFFFFFF) != 0xFFFFFFF; i++)
        c = __bswap_32(pt.fat_ptr[c]);
    return i;
}

off_t x360_offset_token(const char *path, off_t dir, x360_file_record *fr) {
    x360_dir_cluster c;
    int i;
    size_t len = strlen(path);
    pread(fd, &c, sizeof(x360_dir_cluster), dir);
    for (i = 0; i < X360_DIR_MAX; i++) {
        if (strncmp(c[i].filename, path, (len > c[i].fnsize ? len : c[i].fnsize)) == 0) {
            memcpy(fr, c + i, sizeof (x360_file_record));
            return x360_cluster_off_swap(c[i].cluster);
        }
    }
    return -ENOENT;
}

off_t x360_offset(const char *path, off_t root_dir, x360_file_record *fr) {
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

static int x360_modify_file_record(const char *path, x360_function func, void *data) {
    char *dpath = strdupa(path);
    dpath = dirname(dpath);
    char *bpath = strdupa(path);
    bpath = basename(bpath);
    size_t len = strlen(bpath);
    if (len > 42) return -ENAMETOOLONG;

    int i, ret;
    x360_file_record fr;
    x360_dir_cluster c;

    off_t start = x360_offset(dpath, pt.root_dir, &fr);
    if (start < 0) return (int) start;
    if (pread(fd, &c, sizeof(x360_dir_cluster), start) != sizeof(x360_dir_cluster))
        return -ENOENT;

    for (i = 0; i < X360_DIR_MAX; i++) {
        if (memcmp(c[i].filename, bpath, (len > c[i].fnsize ? len : c[i].fnsize)) == 0) {
            ret = func(c + i, data);
            if (ret < 0) return ret;
            if (pwrite(fd, &c, sizeof(x360_dir_cluster), start) != sizeof(x360_dir_cluster))
                return -ENOSPC;
            return ret;
        }
    }
    return -ENOENT;
}

static int x360_new_file_record(const char *path, x360_function func, void *data) {
    char *dpath = strdupa(path);
    dpath = dirname(dpath);
    char *bpath = strdupa(path);
    bpath = basename(bpath);
    size_t len = strlen(bpath);
    if (len > 42) return -ENAMETOOLONG;

    int i, ret;
    x360_file_record fr;
    x360_dir_cluster c;

    off_t start = x360_offset(dpath, pt.root_dir, &fr);
    if (start < 0) return (int) start;
    if (pread(fd, &c, sizeof(x360_dir_cluster), start) != sizeof(x360_dir_cluster))
        return -ENOENT;

    for (i = 0; i < X360_DIR_MAX; i++)
        if (c[i].fnsize != 0xE5 && memcmp(c[i].filename, bpath, (len > c[i].fnsize ? len : c[i].fnsize)) == 0)
            return -EEXIST;

    for (i = 0; i < X360_DIR_MAX; i++) {
        if ((c[i].fnsize == 0xFF) || c[i].fnsize == 0xE5) {
            ret = func(c + i, data);
            if (ret < 0) return ret;
            if (pwrite(fd, &c, sizeof(x360_dir_cluster), start) != sizeof(x360_dir_cluster))
                return -ENOSPC;
            return ret;
        }
    }
    return ENOSPC;
}

static int x360_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    return x360_new_file_record(path, x360_fr_create, path);
}

static int x360_mkdir(const char *path, mode_t mode) {
    return x360_new_file_record(path, x360_fr_mkdir, path);
}

static int x360_rename(const char *old, const char *new) {
    return x360_modify_file_record(old, x360_fr_rename, new);
}

static int x360_rmdir(const char *path) {
    return x360_modify_file_record(path, x360_fr_unlink, NULL);
}

static int x360_truncate(const char *path, off_t size) {
    return x360_modify_file_record(path, x360_fr_truncate, &size);
}

static int x360_unlink(const char *path) {
    return x360_modify_file_record(path, x360_fr_unlink, NULL);
}

static int x360_getattr(const char *path, struct stat *stbuf) {
    x360_file_record fr;

    memset(stbuf, 0, sizeof (struct stat));

    if (strcmp(path, slash) == 0) {
        stbuf->st_mode = S_IFDIR | 0555;
        stbuf->st_nlink = 2;
        return 0;
    }

    off_t start = x360_offset(path, pt.root_dir, &fr);
    if (start < 0) return (int) start;

    stbuf->st_mode = (fr.attribute & 0x10) ? (S_IFDIR | 0555) : (S_IFREG | 0664);
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
    uint32_t cl;
    x360_file_record fr;
    x360_dir_cluster c;

    off_t start = x360_offset(path, pt.root_dir, &fr);
    if (start < 0) return (int) start;
    pread(fd, &c, sizeof(x360_dir_cluster), start);

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

    if (strcmp(path, slash) == 0) return 0;

    cl = __bswap_32(pt.fat_ptr[__bswap_32(fr.cluster)]);
    while ((cl & 0xFFFFFFF) != 0xFFFFFFF) {
        off_t start = x360_cluster_off(cl);
        pread(fd, &c, sizeof(x360_dir_cluster), start);
        for (i = 0; i < X360_DIR_MAX; i++) {
            if (c[i].fnsize == 0xFF) break;
            if (c[i].fnsize < 43) {
                memset(filename, 0, 43);
                memcpy(filename, c[i].filename, c[i].fnsize);
                filler(buf, filename, NULL, 0);
            }
        }
    }

    return 0;
}

static int x360_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    x360_file_record fr;
    size_t total;

    off_t start = x360_offset(path, pt.root_dir, &fr);
    if (offset + size > __bswap_32(fr.fsize))
        size = __bswap_32(fr.fsize) - offset;

    uint32_t cl = offset >> 14;
    cl = x360_cluster(&fr, cl);
    start = x360_cluster_off(cl);
    offset %= 0x4000;
    start += offset;

    total = size;
    while (offset + size > 0x4000) {
        pread(fd, buf, 0x4000 - offset, start);
        cl = __bswap_32(pt.fat_ptr[cl]);
        size -= (0x4000 - offset);
        buf += (0x4000 - offset);
        start = x360_cluster_off(cl);
        offset = 0;
    }
    pread(fd, buf, size, start);
    return total;
}

static int x360_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    x360_file_record fr;
    size_t total;
    off_t start = x360_offset(path, pt.root_dir, &fr);
    if (start < 0) return -ENOENT;
    if ((offset + size) > __bswap_32(fr.fsize)) {
        int i = x360_truncate(path, offset + size);
        if (i < 0) return i;
    }

    uint32_t cl = offset >> 14;
    cl = x360_cluster(&fr, cl);
    start = x360_cluster_off(cl);
    offset %= 0x4000;
    start += offset;

    total = size;
    while (offset + size > 0x4000) {
        pwrite(fd, buf, 0x4000 - offset, start);
        cl = __bswap_32(pt.fat_ptr[cl]);
        size -= (0x4000 - offset);
        buf += (0x4000 - offset);
        start = x360_cluster_off(cl);
        offset = 0;
    }
    pwrite(fd, buf, size, start);
    return total;
}

static struct fuse_operations x360_oper = {
    .create = x360_create,
    .mkdir = x360_mkdir,
    .truncate = x360_truncate,
    .rename = x360_rename,
    .unlink = x360_unlink,
    .rmdir = x360_rmdir,
    .getattr = x360_getattr,
    .readdir = x360_readdir,
    .read = x360_read,
    .write = x360_write,
};

static inline int usage(char *path) {
    fprintf(stderr, "Usage: %s [options] /dev/sdx /mount/point\n\n", basename(path));
    fputs("Options are:\n", stderr);
    fputs("-h         show this message\n", stderr);
    fputs("-o options comma-separated list of options\n", stderr);
    fputs("\tdebug debug mode\n", stderr);
    fputs("\tgid=  set owner gid\n", stderr);
    fputs("\toff=  set file offset\n", stderr);
    fputs("\tuid=  set owner uid\n\n", stderr);
    fprintf(stderr, "Example: %s -o debug,uid=1000 /dev/sdb ~/x360\n", basename(path));
    return -1;
}

int main(int argc, char *argv[]) {
    char *wargv = strdupa(argv[0]);
    char *opt;
    int32_t magic;
    int c, fargc = 2;
    int debug = 0;
    int override = 0;
    struct stat st;
    uid = getuid();
    gid = getgid();
    while ((c = getopt(argc, argv, "ho:")) != -1) {
        switch (c) {
            case 'h':
                usage(wargv);
                break;
            case 'o':
                opt = strtok(optarg, optseparators);
                while (opt != NULL) {
                    switch (option(opt)) {
                        case option_debug:
                            debug = 1;
                            break;
                        case option_gid:
                            opt = strtok(NULL, optseparators);
                            gid = (int) strtoul(opt, NULL, 0);
                            break;
                        case option_off:
                            opt = strtok(NULL, optseparators);
                            pt.start = strtoull(opt, NULL, 0);
                            break;
                        case option_uid:
                            opt = strtok(NULL, optseparators);
                            uid = (int) strtoul(opt, NULL, 0);
                            break;
                    }
                    opt = strtok(NULL, optseparators);
                }
                break;
        }
    }
    if (argc - optind < 2) return usage(wargv);
    fd = open(argv[optind], O_RDWR);
    if (fd < 1) {
        printf("Error opening %s: %s\n", argv[optind], strerror(errno));
        return -1;
    }
    fstat(fd, &st);
    if (!override && S_ISREG(st.st_mode))
        pt.start = 0;
    pread(fd, &magic, sizeof (int32_t), pt.start);
    if (magic != fatx_magic) {
        printf("Error: %s is not a Xbox 360 hard drive or image\n", argv[optind]);
        close(fd);
        return -1;
    }
    x360_partition_calc_size();
    fargc += debug;
    char *fargv[4] = {argv[0], argv[optind + 1], debug ? "-d" : NULL, NULL};
    int ret = fuse_main(fargc, fargv, &x360_oper, NULL);
    msync(pt.fat_ptr, pt.fat_num * sizeof(uint32_t), MS_SYNC);
    munmap(pt.fat_ptr, pt.fat_num * sizeof(uint32_t));
    close(fd);
    return ret;
}
