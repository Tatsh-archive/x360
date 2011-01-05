/*
  xfd: FATX filesystem driver
  Copyright (C) 2010-2011  Isaac Tepper <Isaac356@live.com>

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

#include "fatx.h"
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>

static fatx_fs_info *info;

static struct readdir_user {
	fuse_fill_dir_t filler;
	void *buf;
};

static int xfd_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;
    fatx_file_record record;

    memset(stbuf, 0, sizeof(struct stat));

    res = fatx_read_file_record(&record, info, path);
    if (res < 0) return res;

    if (record.isdir) {
    	stbuf->st_mode = S_IFDIR|0755;
    	stbuf->st_nlink = 2;
    } else {
    	stbuf->st_mode = S_IFREG|0644;
    	stbuf->st_nlink = 1;
    	stbuf->st_size = record.size;
    }
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    stbuf->st_ctim.tv_sec = record.created;
    stbuf->st_mtim.tv_sec = record.modified;
    stbuf->st_atim.tv_sec = record.accessed;

    return res;
}

static void readdir_callback(const char *path, struct readdir_user *user) {
	user->filler(user->buf, path, NULL, 0);
}

static int xfd_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    struct readdir_user user = {
    		.filler = filler,
    		.buf = buf
    };

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    fatx_list_dir(info, path, readdir_callback, &user);

    return 0;
}

static int xfd_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    size_t len;

    len = fatx_read_file(info, path, buf, size, offset);

    return len;
}

static struct fuse_operations xfd_oper = {
    .getattr	= xfd_getattr,
    .readdir	= xfd_readdir,
    .read	= xfd_read
};

int main(int argc, char *argv[])
{
	int debug, fargc, c;
	debug = 0;
	while ((c = getopt(argc, argv, "d")) != -1) {
		switch (c) {
		case 'd':
			debug = 1;
			break;
		}
	}
	fargc = debug ? 3 : 2;
	char *fargv[4] = {argv[0], argv[optind + 1], debug ? "-d" : NULL, NULL};
	info = fatx_fs_init(argv[optind]);
	if (info == NULL) return -1;
    int ret = fuse_main(fargc, fargv, &xfd_oper, NULL);
    fatx_fs_end(info);
    return ret;
}
