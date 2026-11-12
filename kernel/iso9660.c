/* ISO 9660 (CDs and DVDs): reading only. Names come from Rock Ridge (discs made on Linux) or Joliet
 * (discs made on Windows), otherwise the short ISO names. All values from the disc are checked:
 * records must lie inside their block, directories and files inside the volume. */
#include "iso9660.h"
#include <string.h>
#include "cp437.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define BLK      2048
#define MAXDIR   (16u << 20) /* bytes of one directory, more is damage */
#define NOBLOCK  0xFFFFFFFFu
static u32 le32(const u8 *p) {
    return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static u8 u2cp(u32 u) {
    if (u >= 32 && u < 127)
        return (u8)u;
    for (unsigned i = 0; i < sizeof cp437_map / sizeof cp437_map[0]; i++)
        if (cp437_map[i].u == u)
            return cp437_map[i].c;
    return '_';
}
/* n blocks from block b into dst */
static int rblocks(isovol *v, u32 b, u32 n, void *dst) {
    if ((u64)b + n > v->blocks)
        return VE_IO;
    u64 s = v->d.start + (u64)b * 4;
    if (v->d.rdn)
        return v->d.rdn(s, n * 4, dst) ? VE_IO : 0;
    for (u32 i = 0; i < n * 4; i++)
        if (v->d.rd(s + i, (u8 *)dst + i * 512))
            return VE_IO;
    return 0;
}
static const u8 *blk(isovol *v, u32 b) {
    if (v->cached == b)
        return v->buf;
    v->cached = NOBLOCK;
    if (rblocks(v, b, 1, v->buf))
        return 0;
    v->cached = b;
    return v->buf;
}
static u32 unixtime(const u8 *t) { /* the 7 bytes of a directory record */
    int y = 1900 + t[0], m = t[1], d = t[2];
    if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1970)
        return 0;
    /* days since 1970, after the civil calendar */
    int yy = y - (m <= 2), era = yy / 400, yoe = yy - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + doe - 719468;
    long s = days * 86400 + t[3] * 3600 + t[4] * 60 + t[5] - (signed char)t[6] * 15 * 60;
    return s > 0 ? (u32)s : 0;
}
typedef struct {
    u32 lba, len, mtime;
    int dir;
    char name[VFS_NAME + 1];
} ient;
/* the name of a record; 0 = skip it (".", "..", associated files) */
static int rec_name(isovol *v, const u8 *r, int rl, char *o) {
    int nl = r[32], k = 0;
    if (nl == 1 && (r[33] == 0 || r[33] == 1))
        return 0;
    if (r[25] & 4) /* associated file (Mac resource fork) */
        return 0;
    if (v->names == 2) { /* Rock Ridge: NM entries in the system use area */
        int p = 33 + nl + !(nl & 1) + v->susp_skip, got = 0;
        while (p + 4 <= rl) {
            int el = r[p + 2];
            if (el < 4 || p + el > rl)
                break;
            if (r[p] == 'N' && r[p + 1] == 'M' && el >= 5) {
                if (r[p + 4] & 6) /* "." or ".." */
                    return 0;
                for (int i = 5; i < el && k < VFS_NAME;) { /* UTF-8 to CP437 */
                    u32 c = r[p + i++];
                    if (c >= 0xC0 && c < 0xE0 && i < el)
                        c = (c & 31) << 6 | (r[p + i++] & 63);
                    else if (c >= 0xE0 && c < 0xF0 && i + 1 < el) {
                        c = (c & 15) << 12 | (r[p + i] & 63) << 6 | (r[p + i + 1] & 63);
                        i += 2;
                    } else if (c >= 0x80)
                        c = '_';
                    o[k++] = (char)u2cp(c);
                }
                got = 1;
                if (!(r[p + 4] & 1))
                    break;
            }
            if (r[p] == 'S' && r[p + 1] == 'T')
                break;
            p += el;
        }
        if (got && k) {
            o[k] = 0;
            return 1;
        }
        k = 0;
    }
    if (v->names == 1) { /* Joliet: UCS-2, high byte first */
        for (int i = 0; i + 1 < nl && k < VFS_NAME; i += 2) {
            u32 c = (u32)r[33 + i] << 8 | r[34 + i];
            if (c == ';')
                break;
            o[k++] = (char)u2cp(c);
        }
    } else
        for (int i = 0; i < nl && k < VFS_NAME; i++) {
            u8 c = r[33 + i];
            if (c == ';')
                break;
            o[k++] = c >= 32 && c < 127 ? (char)c : '_';
        }
    while (k && o[k - 1] == '.') /* "NAME." of a file without extension */
        k--;
    o[k] = 0;
    for (int i = 0; i < k; i++)
        if (o[i] == '/' || !o[i])
            o[i] = '_';
    return k > 0;
}
/* every entry of the directory at block b with len bytes */
static int dir_each(isovol *v, u32 b, u32 len, int (*cb)(isovol *, ient *, void *), void *a) {
    static ient e;
    if (len > MAXDIR)
        len = MAXDIR;
    u32 nb = (len + BLK - 1) / BLK;
    for (u32 i = 0; i < nb; i++) {
        const u8 *p = blk(v, b + i);
        if (!p)
            return VE_IO;
        u8 copy[BLK];
        memcpy(copy, p, BLK); /* the callback may read other blocks */
        int o = 0;
        while (o + 34 <= BLK) {
            int rl = copy[o];
            if (!rl)
                break; /* the rest of the block is empty */
            if (rl < 34 || o + rl > BLK || 33 + copy[o + 32] > rl)
                break;
            const u8 *r = copy + o;
            o += rl;
            if (!rec_name(v, r, rl, e.name))
                continue;
            e.lba = le32(r + 2);
            e.len = le32(r + 10);
            e.dir = (r[25] & 2) != 0;
            e.mtime = unixtime(r + 18);
            int x = cb(v, &e, a);
            if (x)
                return x;
        }
    }
    return 0;
}
static int lc(int c) {
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if (c == 0x8E)
        return 0x84;
    if (c == 0x99)
        return 0x94;
    if (c == 0x9A)
        return 0x81;
    return c;
}
typedef struct {
    const char *n;
    int l;
    ient r;
    int hit;
} fnd;
static int fcb(isovol *v, ient *e, void *a) {
    (void)v;
    fnd *f = a;
    for (int i = 0; i < f->l; i++)
        if (lc((u8)e->name[i]) != lc((u8)f->n[i]) || !e->name[i])
            return 0;
    if (e->name[f->l])
        return 0;
    f->r = *e;
    f->hit = 1;
    return 1;
}
static int lookup(isovol *v, const char *p, ient *o) {
    if (!v->ok)
        return VE_IO;
    ient cur = {v->root, v->rootn, 0, 1, ""};
    int depth = 0;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *s = p;
        while (*p && *p != '/')
            p++;
        if (!cur.dir)
            return VE_NOTDIR;
        if (++depth > 64)
            return VE_INVAL;
        fnd f = {s, (int)(p - s), {0}, 0};
        int r = dir_each(v, cur.lba, cur.len, fcb, &f);
        if (r < 0)
            return r;
        if (!f.hit)
            return VE_NOENT;
        cur = f.r;
    }
    *o = cur;
    return 0;
}
int iso_mount(isovol *v, const vfs_dev *d) {
    memset(v, 0, sizeof *v);
    v->d = *d;
    v->cached = NOBLOCK;
    v->blocks = (u32)(d->sectors / 4 > 0xFFFFFFFFu ? 0xFFFFFFFFu : d->sectors / 4);
    u32 jroot = 0, jlen = 0, proot = 0, plen = 0;
    int pvd = 0;
    for (u32 b = 16; b < 16 + 64 && b < v->blocks; b++) {
        const u8 *p = blk(v, b);
        if (!p)
            return VE_IO;
        if (memcmp(p + 1, "CD001", 5))
            return VE_FORMAT;
        if (p[0] == 255)
            break;
        if (p[0] == 1 && !pvd) {
            u32 vs = le32(p + 80);
            if (vs && vs < v->blocks)
                v->blocks = vs;
            proot = le32(p + 156 + 2);
            plen = le32(p + 156 + 10);
            pvd = 1;
        }
        if (p[0] == 2 && p[88] == '%' && p[89] == '/' &&
            (p[90] == '@' || p[90] == 'C' || p[90] == 'E')) { /* Joliet */
            jroot = le32(p + 156 + 2);
            jlen = le32(p + 156 + 10);
        }
    }
    if (!pvd || proot >= v->blocks)
        return VE_FORMAT;
    v->root = proot;
    v->rootn = plen;
    v->ok = 1;
    /* Rock Ridge: the root's own record begins its system use area with "SP" */
    const u8 *r = blk(v, proot);
    if (r && r[0] >= 34 && r[32] == 1) {
        int p = 34;
        if (p + 7 <= r[0] && r[p] == 'S' && r[p + 1] == 'P' && r[p + 4] == 0xBE &&
            r[p + 5] == 0xEF) {
            v->names = 2;
            v->susp_skip = r[p + 6];
            return 0;
        }
    }
    if (jroot && jroot < v->blocks) {
        v->names = 1;
        v->root = jroot;
        v->rootn = jlen;
    }
    return 0;
}
int iso_stat(isovol *v, const char *p, vfs_ent *e) {
    ient f;
    int r = lookup(v, p, &f);
    if (r)
        return r;
    if (e) {
        memset(e, 0, sizeof *e);
        const char *b = p, *q;
        for (q = p; *q; q++)
            if (*q == '/' && q[1])
                b = q + 1;
        int k = 0;
        for (; b[k] && b[k] != '/' && k < VFS_NAME; k++)
            e->name[k] = b[k];
        e->name[k] = 0;
        e->dir = f.dir;
        e->size = f.dir ? 0 : f.len;
        e->mtime = f.mtime;
    }
    return 0;
}
int64_t iso_read_at(isovol *v, const char *p, void *b, uint64_t off, uint64_t max) {
    ient f;
    int r = lookup(v, p, &f);
    if (r)
        return r;
    if (f.dir)
        return VE_ISDIR;
    if (off >= f.len)
        return 0;
    if (max > f.len - off)
        max = f.len - off;
    if ((u64)f.lba + (f.len + BLK - 1) / BLK > v->blocks)
        return VE_IO; /* the file would end behind the disc */
    u8 *o = b;
    u64 done = 0;
    while (done < max) {
        u64 at = off + done;
        u32 bl = f.lba + (u32)(at / BLK), in = (u32)(at % BLK);
        u64 left = max - done;
        if (!in && left >= BLK) { /* whole blocks straight into the buffer */
            u32 n = (u32)(left / BLK);
            if (n > 32)
                n = 32;
            if (rblocks(v, bl, n, o + done))
                return VE_IO;
            done += (u64)n * BLK;
            continue;
        }
        const u8 *s = blk(v, bl);
        if (!s)
            return VE_IO;
        u32 k = BLK - in;
        if (k > left)
            k = (u32)left;
        memcpy(o + done, s + in, k);
        done += k;
    }
    return (int64_t)done;
}
int64_t iso_read(isovol *v, const char *p, void *b, uint64_t max) {
    return iso_read_at(v, p, b, 0, max);
}
typedef struct {
    int (*cb)(const vfs_ent *, void *);
    void *a;
} lst;
static int lcb(isovol *v, ient *e, void *a) {
    (void)v;
    lst *l = a;
    static vfs_ent x;
    memset(&x, 0, sizeof x);
    memcpy(x.name, e->name, sizeof x.name);
    x.dir = e->dir;
    x.size = e->dir ? 0 : e->len;
    x.mtime = e->mtime;
    return l->cb(&x, l->a);
}
int iso_list(isovol *v, const char *p, int (*cb)(const vfs_ent *, void *), void *arg) {
    ient f;
    int r = lookup(v, p, &f);
    if (r)
        return r;
    if (!f.dir)
        return VE_NOTDIR;
    lst l = {cb, arg};
    r = dir_each(v, f.lba, f.len, lcb, &l);
    return r < 0 ? r : 0;
}
