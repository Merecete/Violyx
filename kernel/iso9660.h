#pragma once
#include "viofs.h"
/* ISO 9660 for CDs and DVDs, read only, with long names from Joliet or Rock Ridge.
 * Self-contained and testable on the host. Everything on the disc counts as hostile. */
typedef struct {
    vfs_dev d;            /* 512-byte sectors, like the other drivers */
    uint32_t blocks;      /* size of the volume in 2048-byte blocks */
    uint32_t root, rootn; /* the root directory: block and bytes */
    int names;            /* 0 = ISO 9660, 1 = Joliet, 2 = Rock Ridge */
    int susp_skip;        /* Rock Ridge: bytes before the entries in every record */
    uint32_t cached;      /* block in buf, ~0 = none */
    uint8_t buf[2048];
    int ok;
} isovol;
int iso_mount(isovol *v, const vfs_dev *d);
int iso_stat(isovol *v, const char *p, vfs_ent *e);
int64_t iso_read(isovol *v, const char *p, void *b, uint64_t max);
int64_t iso_read_at(isovol *v, const char *p, void *b, uint64_t off, uint64_t max);
int iso_list(isovol *v, const char *p, int (*cb)(const vfs_ent *, void *), void *arg);
