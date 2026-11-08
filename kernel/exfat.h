#pragma once
#include "viofs.h"
/* exFAT driver for removable media, written from the published Microsoft specification.
 * Self-contained and testable on the host. All data on the disk counts as hostile: chains are
 * limited, entry sets are checked against their checksum and every value is range-checked. */
#define EXF_UP 0x500 /* case table for the first code points (Latin, Greek, Cyrillic) */
typedef struct {
    vfs_dev d;
    uint32_t fatoff, fatlen, heap, clusters, root, spc, shift; /* shift: log2 sectors/cluster */
    uint32_t bmfirst, hint;
    uint64_t bmlen;
    int ro, wasdirty, depth, bdirty;
    uint16_t up[EXF_UP];
    uint8_t fb[512], bb[512];
    uint64_t fs, bs;                    /* sectors held in fb (FAT) and bb (bitmap) */
    uint32_t cc_first, cc_idx, cc_clus; /* last position in a FAT chain */
    uint32_t bm_c;                      /* bitmap: cluster bm_ci of its chain */
    uint64_t bm_ci;
    int ok;
} exfvol;
int exf_mount(exfvol *v, const vfs_dev *d);
int exf_stat(exfvol *v, const char *p, vfs_ent *e);
int64_t exf_read(exfvol *v, const char *p, void *b, uint64_t max);
int64_t exf_read_at(exfvol *v, const char *p, void *b, uint64_t off, uint64_t max);
int exf_write(exfvol *v, const char *p, const void *b, uint64_t len, uint32_t now);
int exf_mkdir(exfvol *v, const char *p, uint32_t now);
int exf_remove(exfvol *v, const char *p);
int exf_rename(exfvol *v, const char *a, const char *b);
int exf_list(exfvol *v, const char *p, int (*cb)(const vfs_ent *, void *), void *arg);
