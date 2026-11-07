#include "os.h"
#include "viofs.h"
/* Block layer, IDE driver and VioFS. Safety rule: nothing is EVER formatted automatically. VioFS is
 * only used when a disk has a GPT partition of the Violyx type AND the signature. All other disks
 * are left untouched. */
#define MAXB 8
static blkdev bd[MAXB];
static int nb;
static u32 blk_ids;
blkdev *blk_register(const char *n, u64 s, void *p, int (*r)(blkdev *, u64, void *),
                     int (*w)(blkdev *, u64, const void *), int (*f)(blkdev *)) {
    blkdev *d = 0;
    extern blkdev *fs_device(void);
    for (int i = 0; i < nb; i++)
        if (bd[i].gone && &bd[i] != fs_device())
            d = &bd[i];
    if (!d) {
        if (nb >= MAXB)
            return 0;
        d = &bd[nb++];
    }
    memset(d, 0, sizeof *d);
    d->name = n;
    d->sectors = s;
    d->p = p;
    d->rd = r;
    d->wr = w;
    d->fl = f;
    d->id = ++blk_ids;
    return d;
}
void blk_gone(blkdev *d) {
    d->gone = 1;
}
void blk_model_ata(blkdev *d, const u16 *w) {
    for (int i = 0; i < 20; i++) {
        d->model[i * 2] = w[27 + i] >> 8;
        d->model[i * 2 + 1] = w[27 + i] & 255;
    }
    d->model[40] = 0;
    for (int i = 39; i >= 0 && d->model[i] == ' '; i--)
        d->model[i] = 0;
}
int blk_count(void) {
    int c = 0;
    for (int i = 0; i < nb; i++)
        c += !bd[i].gone;
    return c;
}
blkdev *blk_get(int n) {
    for (int i = 0; i < nb; i++)
        if (!bd[i].gone && n-- == 0)
            return &bd[i];
    return 0;
}
static blkdev *dev;
static u64 FS_LBA, FS_N;
static int ok;
static const char *st = "nicht initialisiert", *sten = "not initialised";
#define SETST(a, b) (st = (a), sten = (b))
/* IDE: all four positions (two channels with master and slave each). CD drives are skipped. */
typedef struct {
    u16 base, ctrl;
    u8 sl;
    u8 l48;
} ide_t;
static ide_t ide[4];
static int nide;
static int wait_nbsy(u16 b) {
    for (u32 i = 0; i < 2000000; i++) {
        u8 s = inb(b + 7);
        if (s == 0xFF)
            return -1;
        if (!(s & 0x80))
            return s;
    }
    return -1;
}
static int wait_drq(u16 b) {
    for (u32 i = 0; i < 2000000; i++) {
        u8 s = inb(b + 7);
        if (s == 0xFF || (s & 1))
            return -1;
        if (!(s & 0x80) && (s & 8))
            return 0;
    }
    return -1;
}
static void sel(ide_t *d, u8 hi) {
    outb(d->base + 6, 0xE0 | (d->sl << 4) | hi);
    for (int i = 0; i < 4; i++)
        inb(d->ctrl);
}
static void go(ide_t *d, u64 l, u8 c28, u8 c48) {
    u16 b = d->base;
    if (d->l48) {
        sel(d, 0);
        outb(b + 2, 0);
        outb(b + 3, l >> 24);
        outb(b + 4, l >> 32);
        outb(b + 5, l >> 40);
        outb(b + 2, 1);
        outb(b + 3, l);
        outb(b + 4, l >> 8);
        outb(b + 5, l >> 16);
        outb(b + 7, c48);
    } else {
        sel(d, (l >> 24) & 15);
        outb(b + 2, 1);
        outb(b + 3, l);
        outb(b + 4, l >> 8);
        outb(b + 5, l >> 16);
        outb(b + 7, c28);
    }
}
static int ide_rd(blkdev *x, u64 lba, void *b) {
    ide_t *d = x->p;
    sel(d, 0);
    if (wait_nbsy(d->base) < 0)
        return -1;
    go(d, lba, 0x20, 0x24);
    if (wait_drq(d->base))
        return -1;
    u16 *p = b;
    for (int i = 0; i < 256; i++)
        p[i] = inw(d->base);
    return 0;
}
static int ide_wr(blkdev *x, u64 lba, const void *b) {
    ide_t *d = x->p;
    sel(d, 0);
    if (wait_nbsy(d->base) < 0)
        return -1;
    go(d, lba, 0x30, 0x34);
    if (wait_drq(d->base))
        return -1;
    const u16 *p = b;
    for (int i = 0; i < 256; i++)
        outw(d->base, p[i]);
    return wait_nbsy(d->base) < 0 ? -1 : 0;
}
static int ide_fl(blkdev *x) {
    ide_t *d = x->p;
    sel(d, 0);
    if (wait_nbsy(d->base) >= 0) {
        outb(d->base + 7, d->l48 ? 0xEA : 0xE7);
        wait_nbsy(d->base);
    }
    return 0;
}
static void ide_one(u16 base, u16 ctrl, u8 sl) {
    if (nide >= 4)
        return;
    outb(ctrl, 2);
    ide_t *d = &ide[nide];
    d->base = base;
    d->ctrl = ctrl;
    d->sl = sl;
    d->l48 = 0;
    outb(base + 6, 0xA0 | (sl << 4));
    for (int i = 0; i < 15; i++)
        inb(ctrl);
    u8 s = inb(base + 7);
    if (s == 0xFF || s == 0)
        return;
    for (u32 i = 0; i < 500000 && (inb(base + 7) & 0x80); i++)
        ;
    if (inb(base + 4) == 0x14 && inb(base + 5) == 0xEB)
        return; /* ATAPI */
    outb(base + 2, 0);
    outb(base + 3, 0);
    outb(base + 4, 0);
    outb(base + 5, 0);
    outb(base + 7, 0xEC);
    for (int i = 0; i < 15; i++)
        inb(ctrl);
    if (!inb(base + 7))
        return;
    if (inb(base + 4) || inb(base + 5))
        return;
    if (wait_drq(base))
        return;
    static u16 w[256];
    for (int i = 0; i < 256; i++)
        w[i] = inw(base);
    u64 sec;
    if (w[83] & (1 << 10)) {
        sec = *(u64 *)(w + 100);
        d->l48 = 1;
    } else
        sec = *(u32 *)(w + 60);
    if (!sec)
        return;
    nide++;
    blk_register("IDE", sec, d, ide_rd, ide_wr, ide_fl);
    blk_model_ata(blk_get(nb - 1), w);
}
void ide_probe(void) {
    ide_one(0x1F0, 0x3F6, 0);
    ide_one(0x1F0, 0x3F6, 1);
    ide_one(0x170, 0x376, 0);
    ide_one(0x170, 0x376, 1);
}
static int v_rd(u64 l, void *b) {
    return dev->rd(dev, l, b);
}
static int v_wr(u64 l, const void *b) {
    return dev->wr(dev, l, b);
}
static int v_fl(void) {
    return dev->fl(dev);
}
static const char GUID[16] = "VIOLYXFSPARTITN1";
u32 fs_now(void) {
    rtc_t r;
    rtc_read(&r);
    return rtc_unix(&r);
}
static const char *DIRS[] = {"/system", "/programs", "/home", "/downloads", 0};
/* Finds the Violyx GPT partition. 1 = found, 2 = old version */
static int probe(blkdev *d) {
    static u8 s[512];
    if (d->rd(d, 1, s) || memcmp(s, "EFI PART", 8))
        return 0;
    u64 el = *(u64 *)(s + 72);
    u32 cnt = *(u32 *)(s + 80), esz = *(u32 *)(s + 84);
    if (esz < 128 || esz > 512 || cnt > 512)
        return 0;
    for (u32 i = 0; i < cnt; i++) {
        u64 sec = el + i * esz / 512;
        if (d->rd(d, sec, s))
            return 0;
        u8 *e = s + (i * esz) % 512;
        if (memcmp(e, GUID, 16))
            continue;
        u64 lba = *(u64 *)(e + 32), last = *(u64 *)(e + 40);
        if (last < lba)
            return 0;
        static u8 h[512];
        if (d->rd(d, lba, h))
            return 0;
        if (!memcmp(h, "VIOLYXFS", 8))
            return 2;
        int fresh = !memcmp(h, "VIOFSNEW", 8);
        if (!fresh && memcmp(h, "VIOFS2\0\0", 8))
            return 0;
        dev = d;
        FS_LBA = lba;
        FS_N = last - lba + 1;
        vfs_dev v = {v_rd, v_wr, v_fl, lba, FS_N};
        if (fresh) {
            if (vfs_format(&v, fs_now()))
                return 0;
            for (int k = 0; DIRS[k]; k++)
                vfs_mkdir(DIRS[k], fs_now());
            return 1;
        }
        return vfs_mount(&v) ? 0 : 1;
    }
    return 0;
}
static char stbuf[48];
int fs_init(void) {
    if (!nb) {
        SETST("kein beschreibbarer Datenträger", "no writable storage");
        return -1;
    }
    int old = 0;
    for (int i = 0; i < nb; i++) {
        if (bd[i].gone)
            continue;
        int r = probe(&bd[i]);
        if (r == 1) {
            ok = 1;
            return 0;
        }
        if (r == 2)
            old = 1;
    }
    if (old)
        SETST("altes Format, bitte neu installieren", "old format, please reinstall");
    else
        SETST("kein VioFS (Medien unberührt)", "no VioFS (media untouched)");
    return -1;
}
int fs_attach(blkdev *d) {
    if (probe(d) != 1)
        return -1;
    ok = 1;
    return 0;
}
void fs_detach(void) {
    vfs_unmount();
    ok = 0;
    dev = 0;
}
blkdev *fs_device(void) {
    return ok ? dev : 0;
}
int fs_ok(void) {
    return ok;
}
const char *fs_status(void) {
    if (ok) {
        ksnprintf(stbuf, 48, TR("bereit (%s)", "ready (%s)"), dev->name);
        return stbuf;
    }
    return TR(st, sten);
}
