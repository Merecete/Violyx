#pragma once
#include "viofs.h"
/* FAT32 driver for removable media. Self-contained and testable on the host. All data on the disk
 * counts as hostile: chains are limited and every value is checked. */
typedef struct {
    vfs_dev d;
    uint32_t spc, rsvd, nfat, fatsz, root, clusters, datasec, hint, fsinfo;
    int fsdirty;
    uint32_t cs;
    uint32_t cc_first, cc_idx, cc_clus; /* last position in a cluster chain */
    uint8_t buf[512];
    int ok;
} fatvol;
int fat_mount(fatvol *v, const vfs_dev *d);
int fat_stat(fatvol *v, const char *p, vfs_ent *e);
int64_t fat_read(fatvol *v, const char *p, void *b, uint64_t max);
int64_t fat_read_at(fatvol *v, const char *p, void *b, uint64_t off, uint64_t max);
int fat_write(fatvol *v, const char *p, const void *b, uint64_t len, uint32_t now);
int fat_mkdir(fatvol *v, const char *p, uint32_t now);
int fat_remove(fatvol *v, const char *p);
int fat_rename(fatvol *v, const char *a, const char *b);
int fat_list(fatvol *v, const char *p, int (*cb)(const vfs_ent *, void *), void *arg);
