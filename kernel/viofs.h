#pragma once
#include <stdint.h>
#include <stddef.h>
/* VioFS 2: a self-contained file system, also testable on the host. Block size 4 KiB, inodes with
 * direct and indirect blocks, directories with fixed-size entries. */
#define VFS_NAME 250
#define VFS_PATH 512
enum {
    VE_OK = 0,
    VE_NOENT = -1,
    VE_EXIST = -2,
    VE_NOSPC = -3,
    VE_IO = -4,
    VE_NOTDIR = -5,
    VE_NOTEMPTY = -6,
    VE_INVAL = -7,
    VE_ISDIR = -8,
    VE_DENIED = -9,
    VE_FORMAT = -10,
    VE_NOFS = -11
};
typedef struct {
    int (*rd)(uint64_t sec, void *);
    int (*wr)(uint64_t sec, const void *);
    int (*fl)(void);
    uint64_t start, sectors;
    /* optional: n sectors at once; 0 = one by one */
    int (*rdn)(uint64_t sec, uint32_t n, void *);
    int (*wrn)(uint64_t sec, uint32_t n, const void *);
} vfs_dev;
typedef struct {
    char name[VFS_NAME + 1];
    int dir;
    uint64_t size;
    uint32_t mtime;
} vfs_ent;
int vfs_format(const vfs_dev *d, uint32_t now);
int vfs_mount(const vfs_dev *d);
void vfs_unmount(void);
int vfs_mounted(void);
int vfs_stat(const char *path, vfs_ent *e);
int64_t vfs_read(const char *path, void *buf, uint64_t max);
int64_t vfs_read_at(const char *path, void *buf, uint64_t off, uint64_t max);
int vfs_write(const char *path, const void *buf, uint64_t len, uint32_t now);
int vfs_mkdir(const char *path, uint32_t now);
int vfs_remove(const char *path);
int vfs_rename(const char *from, const char *to);
int vfs_list(const char *path, int (*cb)(const vfs_ent *, void *), void *arg);
void vfs_usage(uint64_t *total, uint64_t *freeb, uint32_t *files);
int vfs_norm(const char *cwd, const char *in, char *out, size_t n);
const char *vfs_errstr(int e, int en);
