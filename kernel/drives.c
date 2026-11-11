#include "os.h"
#include "fat.h"
#include "exfat.h"
#include "format.h"
/* Drives: puts USB disks under /drives on top of VioFS. Rules: only USB devices appear here, never
 * internal disks. Violyx asks before the first access, and until then not a single sector of the
 * stick is read. The prompt sits here on purpose, in one central place, so no program can get
 * around it. */
#define MAXD 8
typedef struct {
    blkdev *d;
    u32 id;
    int allowed, st, ex; /* ex: the drive uses exFAT */
    fatvol v;
    exfvol x;
} drv_t;
static drv_t dr[MAXD];
static blkdev *cur;
u32 fsx_gen = 1;
static int t_rd(uint64_t s, void *b) {
    return cur && !cur->gone ? cur->rd(cur, s, b) : -1;
}
static int t_wr(uint64_t s, const void *b) {
    return cur && !cur->gone ? cur->wr(cur, s, b) : -1;
}
static int t_rdn(uint64_t s, uint32_t n, void *b) {
    return cur && !cur->gone ? cur->rdn(cur, s, n, b) : -1;
}
static int t_wrn(uint64_t s, uint32_t n, const void *b) {
    return cur && !cur->gone ? cur->wrn(cur, s, n, b) : -1;
}
static int t_fl(void) {
    return cur && !cur->gone ? cur->fl(cur) : -1;
}
void drives_sync(void) {
    for (int i = 0; i < MAXD; i++)
        if (dr[i].d && (dr[i].d->gone || dr[i].d->id != dr[i].id)) {
            dr[i].d = 0;
            dr[i].allowed = 0;
            dr[i].st = 0;
            fsx_gen++;
        }
    for (int j = 0; j < blk_count(); j++) {
        blkdev *d = blk_get(j);
        if (!d->usb || d == fs_device())
            continue;
        int have = 0;
        for (int i = 0; i < MAXD; i++)
            if (dr[i].d == d)
                have = 1;
        if (have)
            continue;
        for (int i = 0; i < MAXD; i++)
            if (!dr[i].d) {
                dr[i].d = d;
                dr[i].id = d->id;
                dr[i].allowed = 0;
                dr[i].st = 0;
                fsx_gen++;
                break;
            }
    }
    wall_photo_check();
}
void drives_reset(void) {
    for (int i = 0; i < MAXD; i++) {
        dr[i].allowed = 0;
        dr[i].st = 0;
    }
    fsx_gen++;
    drives_sync();
}
/* Finds the FAT32 or exFAT partition. Data partitions only: EFI system partitions and VioFS are
 * left untouched */
static int try_(drv_t *x, u64 st, u64 n) {
    if (st >= cur->sectors)
        return VE_FORMAT;
    if (st + n > cur->sectors || !n)
        n = cur->sectors - st;
    vfs_dev v = {t_rd, t_wr, t_fl, st, n, cur->rdn ? t_rdn : 0, cur->wrn ? t_wrn : 0};
    x->ex = 0;
    int r = fat_mount(&x->v, &v);
    if (r != VE_FORMAT)
        return r;
    r = exf_mount(&x->x, &v);
    if (!r)
        x->ex = 1;
    return r;
}
static const u8 BASIC[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
                             0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
static int find_fat(drv_t *x) {
    static u8 s[512], h[512];
    if (cur->rd(cur, 1, h))
        return VE_IO;
    if (!memcmp(h, "EFI PART", 8)) {
        u64 el = *(u64 *)(h + 72);
        u32 cnt = *(u32 *)(h + 80), esz = *(u32 *)(h + 84);
        if (esz < 128 || esz > 512 || (esz & 127) || cnt > 256)
            return VE_FORMAT;
        for (u32 i = 0; i < cnt; i++) {
            if (cur->rd(cur, el + i * esz / 512, s))
                return VE_IO;
            u8 *e = s + (i * esz) % 512;
            if (memcmp(e, BASIC, 16))
                continue;
            u64 a = *(u64 *)(e + 32), b = *(u64 *)(e + 40);
            if (b < a)
                continue;
            int r = try_(x, a, b - a + 1);
            if (r != VE_FORMAT)
                return r;
        }
        return VE_FORMAT;
    }
    if (cur->rd(cur, 0, s))
        return VE_IO;
    if (s[510] != 0x55 || s[511] != 0xAA)
        return VE_FORMAT;
    int r = try_(x, 0, cur->sectors);
    if (r != VE_FORMAT)
        return r;
    for (int i = 0; i < 4; i++) {
        u8 *e = s + 446 + i * 16;
        if (e[4] != 0x0B && e[4] != 0x0C && e[4] != 0x1C && e[4] != 0x07)
            continue; /* FAT32 or exFAT (0x07 is shared with NTFS, the mount tells them apart) */
        u32 a, n;
        memcpy(&a, e + 8, 4);
        memcpy(&n, e + 12, 4);
        static u8 k[512];
        memcpy(k, s, 512);
        r = try_(x, a, n);
        if (r != VE_FORMAT)
            return r;
        memcpy(s, k, 512);
    }
    return VE_FORMAT;
}
static void desc(drv_t *x, char *o, int n) {
    u64 mb = x->d->sectors / 2048;
    const char *m = x->d->model[0] ? x->d->model : "USB";
    const char *t = x->allowed && x->st == 1 ? (x->ex ? ", exFAT" : ", FAT32") : "";
    if (mb >= 2048)
        ksnprintf(o, n, "%s, %ld GB%s", m, mb / 1024, t);
    else
        ksnprintf(o, n, "%s, %ld MB%s", m, mb, t);
}
/* 0 = VioFS, 1 = /drives itself, 2 = on a drive */
static int split(const char *p, int *i, const char **rel) {
    if (memcmp(p, "/drives", 7) || (p[7] && p[7] != '/'))
        return 0;
    const char *q = p + 7;
    while (*q == '/')
        q++;
    if (!*q)
        return 1;
    *i = -1;
    *rel = "/";
    if (!memcmp(q, "usb", 3) && q[3] >= '1' && q[3] <= '8' && (!q[4] || q[4] == '/')) {
        *i = q[3] - '1';
        if (q[4])
            *rel = q + 4;
    }
    return 2;
}
static int open_(int i) {
    if (i < 0 || i >= MAXD || !dr[i].d || dr[i].d->gone)
        return VE_NOENT;
    drv_t *x = &dr[i];
    if (!x->allowed) {
        char t[40], s[72], m[60];
        u32 id = x->id;
        desc(x, m, 60);
        ksnprintf(t, 40, TR("Laufwerk usb%d öffnen", "Open drive usb%d"), i + 1);
        ksnprintf(s, 72, TR("%s: Zugriff erlauben?", "%s: allow access?"), m);
        int yes = dlg_confirm(t, s);
        if (!x->d || x->d->gone || x->id != id)
            return VE_NOENT;
        if (!yes)
            return VE_DENIED;
        x->allowed = 1;
        x->st = 0;
        fsx_gen++;
    }
    cur = x->d;
    if (x->st == 0) {
        x->st = find_fat(x);
        if (!x->st)
            x->st = 1;
    }
    if (x->st < 0) {
        int r = x->st;
        if (r == VE_IO)
            x->st = 0;
        return r;
    }
    return 0;
}
int fsx_stat(const char *p, vfs_ent *e) {
    int i;
    const char *r;
    int k = split(p, &i, &r);
    if (k == 1 || (k == 0 && !fs_ok() && !strcmp(p, "/"))) {
        if (e) {
            memset(e, 0, sizeof *e);
            e->dir = 1;
        }
        return 0;
    }
    if (k == 0)
        return fs_ok() ? vfs_stat(p, e) : VE_NOFS;
    int o = open_(i);
    return o ? o : dr[i].ex ? exf_stat(&dr[i].x, r, e) : fat_stat(&dr[i].v, r, e);
}
/* Is the file there? Without asking the user and without opening the drive for programs: only
 * for the background picture, which falls back to the chosen background when its stick is gone */
int fsx_exists(const char *p) {
    int i;
    const char *r;
    vfs_ent e;
    int k = split(p, &i, &r);
    if (k == 0)
        return fs_ok() && !vfs_stat(p, &e) && !e.dir;
    if (k == 1 || i < 0 || i >= MAXD || !dr[i].d || dr[i].d->gone)
        return 0;
    drv_t *x = &dr[i];
    int al = x->allowed, st = x->st;
    x->allowed = 1; /* no question, only for this look */
    int o = open_(i);
    int ok = !o && !(x->ex ? exf_stat(&x->x, r, &e) : fat_stat(&x->v, r, &e)) && !e.dir;
    if (!al) {
        x->allowed = 0;
        x->st = st;
    }
    return ok;
}
int64_t fsx_read(const char *p, void *b, u64 m) {
    int i;
    const char *r;
    int k = split(p, &i, &r);
    if (k == 1)
        return VE_ISDIR;
    if (k == 0)
        return fs_ok() ? vfs_read(p, b, m) : VE_NOFS;
    int o = open_(i);
    return o ? o : dr[i].ex ? exf_read(&dr[i].x, r, b, m) : fat_read(&dr[i].v, r, b, m);
}
/* A part of a file: up to m bytes from offset off on */
int64_t fsx_read_at(const char *p, void *b, u64 off, u64 m) {
    int i;
    const char *r;
    int k = split(p, &i, &r);
    if (k == 1)
        return VE_ISDIR;
    if (k == 0)
        return fs_ok() ? vfs_read_at(p, b, off, m) : VE_NOFS;
    int o = open_(i);
    return o          ? o
           : dr[i].ex ? exf_read_at(&dr[i].x, r, b, off, m)
                      : fat_read_at(&dr[i].v, r, b, off, m);
}
int fsx_write(const char *p, const void *b, u64 l, u32 now) {
    int i, x;
    const char *r;
    int k = split(p, &i, &r);
    if (k == 1)
        return VE_ISDIR;
    if (k == 0)
        x = fs_ok() ? vfs_write(p, b, l, now) : VE_NOFS;
    else {
        x = open_(i);
        if (!x)
            x = dr[i].ex ? exf_write(&dr[i].x, r, b, l, now) : fat_write(&dr[i].v, r, b, l, now);
    }
    fsx_gen++;
    return x;
}
int fsx_mkdir(const char *p, u32 now) {
    int i, x;
    const char *r;
    int k = split(p, &i, &r);
    if (k == 1)
        return VE_DENIED;
    if (k == 0)
        x = fs_ok() ? vfs_mkdir(p, now) : VE_NOFS;
    else {
        x = open_(i);
        if (!x)
            x = dr[i].ex ? exf_mkdir(&dr[i].x, r, now) : fat_mkdir(&dr[i].v, r, now);
    }
    fsx_gen++;
    return x;
}
int fsx_protected(const char *p) {
    int i;
    const char *r;
    int k = split(p, &i, &r);
    if (k == 1)
        return 1;
    if (k == 2) {
        while (*r == '/')
            r++;
        return !*r;
    }
    return 0;
}
int fsx_remove(const char *p) {
    int i, x;
    const char *r;
    if (fsx_protected(p))
        return VE_DENIED;
    int k = split(p, &i, &r);
    if (k == 0)
        x = fs_ok() ? vfs_remove(p) : VE_NOFS;
    else {
        x = open_(i);
        if (!x)
            x = dr[i].ex ? exf_remove(&dr[i].x, r) : fat_remove(&dr[i].v, r);
    }
    fsx_gen++;
    return x;
}
typedef struct {
    int (*cb)(const vfs_ent *, void *);
    void *a;
} rc_t;
static int rootcb(const vfs_ent *e, void *x) {
    rc_t *c = x;
    return strcmp(e->name, "drives") ? c->cb(e, c->a) : 0;
}
int fsx_list(const char *p, int (*cb)(const vfs_ent *, void *), void *a) {
    int i;
    const char *r;
    int k = split(p, &i, &r);
    static vfs_ent e;
    if (k == 1) {
        for (int n = 0; n < MAXD; n++)
            if (dr[n].d) {
                memset(&e, 0, sizeof e);
                ksnprintf(e.name, 8, "usb%d", n + 1);
                e.dir = 1;
                if (cb(&e, a))
                    break;
            }
        return 0;
    }
    if (k == 2) {
        int o = open_(i);
        return o ? o : dr[i].ex ? exf_list(&dr[i].x, r, cb, a) : fat_list(&dr[i].v, r, cb, a);
    }
    if (!strcmp(p, "/")) {
        rc_t c = {cb, a};
        if (fs_ok()) {
            int x = vfs_list(p, rootcb, &c);
            if (x < 0)
                return x;
        }
        memset(&e, 0, sizeof e);
        strlcpy_(e.name, "drives", 8);
        e.dir = 1;
        cb(&e, a);
        return 0;
    }
    return fs_ok() ? vfs_list(p, cb, a) : VE_NOFS;
}
int fsx_rmtree(const char *p) {
    if (fsx_protected(p))
        return VE_DENIED;
    vfs_ent e;
    int r = fsx_stat(p, &e);
    if (r)
        return r;
    if (e.dir) {
        fs_item *it;
        int n = fs_load(p, &it);
        if (n < 0) {
            kfree(it);
            return VE_IO;
        }
        char *c = kmalloc(VFS_PATH + 260);
        if (!c) {
            kfree(it);
            return VE_NOSPC;
        }
        for (int i = 0; i < n; i++) {
            ksnprintf(c, VFS_PATH + 260, "%s/%s", p, it[i].name);
            r = fsx_rmtree(c);
            if (r) {
                kfree(c);
                kfree(it);
                return r;
            }
        }
        kfree(c);
        kfree(it);
    }
    return fsx_remove(p);
}
static int under(const char *a, const char *b) {
    size_t l = strlen(a);
    return strlen(b) >= l && !memcmp(a, b, l) && (!b[l] || b[l] == '/');
}
int fsx_copy(const char *a, const char *b) {
    if (under(a, b))
        return VE_INVAL;
    if (fsx_protected(a) || fsx_protected(b))
        return VE_DENIED;
    vfs_ent e;
    int r = fsx_stat(a, &e);
    if (r)
        return r;
    if (e.dir) {
        r = fsx_mkdir(b, fs_now());
        if (r && r != VE_EXIST)
            return r;
        fs_item *it;
        int n = fs_load(a, &it);
        if (n < 0) {
            kfree(it);
            return VE_IO;
        }
        char *x = kmalloc(2 * (VFS_PATH + 260));
        if (!x) {
            kfree(it);
            return VE_NOSPC;
        }
        char *y = x + VFS_PATH + 260;
        r = 0;
        for (int i = 0; i < n && !r; i++) {
            ksnprintf(x, VFS_PATH + 260, "%s/%s", a, it[i].name);
            ksnprintf(y, VFS_PATH + 260, "%s/%s", b, it[i].name);
            if (strlen(x) >= VFS_PATH || strlen(y) >= VFS_PATH)
                r = VE_INVAL;
            else
                r = fsx_copy(x, y);
        }
        kfree(x);
        kfree(it);
        return r;
    }
    u64 sz = e.size;
    char *m = kmalloc(sz + 1);
    if (!m)
        return VE_NOSPC;
    int64_t n = fsx_read(a, m, sz);
    r = n < 0 ? (int)n : fsx_write(b, m, (u64)n, fs_now());
    kfree(m);
    return r;
}
int fsx_rename(const char *a, const char *b) {
    int i = -1, j = -1;
    const char *r, *s;
    if (fsx_protected(a) || fsx_protected(b))
        return VE_DENIED;
    int k = split(a, &i, &r), l = split(b, &j, &s);
    int x;
    if (k == 0 && l == 0)
        x = fs_ok() ? vfs_rename(a, b) : VE_NOFS;
    else if (k == 2 && l == 2 && i == j) {
        x = open_(i);
        if (!x)
            x = dr[i].ex ? exf_rename(&dr[i].x, r, s) : fat_rename(&dr[i].v, r, s);
    } else {
        if (under(a, b))
            return VE_INVAL;
        vfs_ent e;
        x = fsx_stat(b, &e);
        if (!x)
            return VE_EXIST;
        if (x != VE_NOENT)
            return x;
        x = fsx_copy(a, b);
        if (!x)
            x = fsx_rmtree(a);
    }
    fsx_gen++;
    return x;
}
int fsx_quiet(const char *p) {
    int i;
    const char *r;
    if (split(p, &i, &r) != 2)
        return 1;
    return i >= 0 && i < MAXD && dr[i].d && !dr[i].d->gone && dr[i].allowed && dr[i].st >= 0;
}
int fsx_first(char *o, int n) {
    for (int i = 0; i < MAXD; i++)
        if (dr[i].d) {
            ksnprintf(o, n, "/drives/usb%d", i + 1);
            return 1;
        }
    return 0;
}
const char *fsx_home(void) {
    static char b[24];
    if (fs_ok())
        return "/home";
    if (fsx_first(b, 24))
        return b;
    return "/drives";
}
void fsx_info(const char *name, char *o, int n) {
    o[0] = 0;
    char p[32];
    int i;
    const char *r;
    ksnprintf(p, 32, "/drives/%s", name);
    if (split(p, &i, &r) == 2 && i >= 0 && dr[i].d)
        desc(&dr[i], o, n);
}
/* Formatting: the caller has asked the user. Only USB drives in /drives, never the drive Violyx
 * runs from. */
static blkdev *fdev;
static int f_rd(uint64_t s, void *b) {
    return fdev && !fdev->gone ? fdev->rd(fdev, s, b) : -1;
}
static int f_wr(uint64_t s, const void *b) {
    return fdev && !fdev->gone ? fdev->wr(fdev, s, b) : -1;
}
static int f_wrn(uint64_t s, uint32_t n, const void *b) {
    return fdev && !fdev->gone ? fdev->wrn(fdev, s, n, b) : -1;
}
static int f_fl(void) {
    return fdev && !fdev->gone ? fdev->fl(fdev) : -1;
}
static int drv_index(const char *name) {
    char p[VFS_PATH];
    int i;
    const char *r;
    ksnprintf(p, VFS_PATH, "/drives/%s", name[0] == '/' ? name + 8 : name);
    if (split(p, &i, &r) != 2 || i < 0 || i >= MAXD || !dr[i].d || dr[i].d->gone)
        return -1;
    return i;
}
int fsx_can_format(const char *name) {
    int i = drv_index(name);
    if (i < 0)
        return VE_NOENT;
    blkdev *d = dr[i].d;
    if (!d->usb || d == fs_device() || boot_drive(d))
        return VE_DENIED;
    return 0;
}
int fsx_format(const char *name, int exfat, const char *label, void (*prog)(u64, u64)) {
    int r = fsx_can_format(name);
    if (r)
        return r;
    int i = drv_index(name);
    fdev = dr[i].d;
    vfs_dev v = {f_rd, f_wr, f_fl, 0, fdev->sectors, 0, fdev->wrn ? f_wrn : 0};
    r = exfat ? fmt_exfat(&v, label, (u32)(ticks * 2654435761u) ^ 0x56494F4C, prog)
              : fmt_fat32(&v, label, (u32)(ticks * 2654435761u) ^ 0x56494F4C, prog);
    fdev = 0;
    dr[i].allowed = 1; /* formatted with consent: the drive may be used right away */
    dr[i].st = 0;
    fsx_gen++;
    return r;
}
const char *fsx_type(const char *name) {
    int i = drv_index(name);
    if (i < 0 || !dr[i].allowed || dr[i].st != 1)
        return 0;
    return dr[i].ex ? "exFAT" : "FAT32";
}
int fsx_eject(const char *name) {
    char p[VFS_PATH];
    int i;
    const char *r;
    ksnprintf(p, VFS_PATH, "/drives/%s", name[0] == '/' ? name + 8 : name);
    if (split(p, &i, &r) != 2 || i < 0 || i >= MAXD || !dr[i].d)
        return VE_NOENT;
    if (dr[i].allowed && !dr[i].d->gone)
        dr[i].d->fl(dr[i].d);
    dr[i].allowed = 0;
    dr[i].st = 0;
    fsx_gen++;
    return 0;
}
