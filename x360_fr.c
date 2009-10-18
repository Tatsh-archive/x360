#include "x360.h"

extern x360_partition pt;

int x360_fr_create(x360_file_record *fr, void *data) {
    int ret;
    uint32_t cl;
    memset(fr, 0, sizeof(x360_file_record));
    ret = x360_fr_rename(fr, data);
    if (ret < 0) return ret;
    cl = x360_find_free_cluster();
    if (cl == 0) return -ENOSPC;
    fr->cluster = __bswap_32(cl);
    pt.fat_ptr[cl] = 0xFFFFFFFF;
    return 0;
}

int x360_fr_mkdir(x360_file_record *fr, void *data) {
    int ret;
    ret = x360_fr_create(fr, data);
    if (ret < 0) return ret;
    fr->attribute |= 0x10;
    return 0;
}

int x360_fr_rename(x360_file_record *fr, void *data) {
    char *new = (char *) data;
    char *base = strdupa(new);
    base = basename(base);
    size_t len = strlen(base);
    if (len > 42) return -ENAMETOOLONG;
    fr->fnsize = len;
    memset(fr->filename, 0xFF, 42);
    memcpy(fr->filename, base, len);
    return 0;
}

int x360_fr_truncate(x360_file_record *fr, void *data) {
    off_t *size = (off_t *) data;
    uint32_t cl, cl2;
    uint32_t max = x360_clusters(fr) << 14;

    while (*size > max) {
        cl = x360_cluster(fr, (max >> 14) - 1);
        cl2 = x360_find_free_cluster();
        if (cl2 == 0) return -ENOSPC;
        pt.fat_ptr[cl] = __bswap_32(cl2);
        max += 0x4000;
    }

    while (*size <= (max - 0x4000)) {
        if (*size == 0) break;
        cl = x360_cluster(fr, (max >> 14) - 1);
        if (cl == 0) return -EIO;
        pt.fat_ptr[cl] = 0;
        max -= 0x4000;
    }

    cl = x360_cluster(fr, (max >> 14) - 1);
    pt.fat_ptr[cl] = 0xFFFFFFFF;
    fr->fsize = __bswap_32(*size);
    return 0;
}

int x360_fr_unlink(x360_file_record *fr, void *data) {
    (void) data;
    int32_t cl, cl2;
    fr->fnsize = 0xE5;
    cl = __bswap_32(fr->cluster);
    while ((cl & 0xFFFFFFF) != 0xFFFFFFF) {
        cl2 = __bswap_32(pt.fat_ptr[cl]);
        pt.fat_ptr[cl] = 0;
        cl = cl2;
    }
    return 0;
}
