#include "os.h"
#include "viofs.h"
/* Block layer, IDE driver and VioFS. Safety rule: nothing is EVER formatted automatically. VioFS is
 * only used when a disk has a GPT partition of the Violyx type AND the signature. All other disks
 * are left untouched. */
#define MAXB 16
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
/* Shutdown and restart: every drive empties its write cache. Before power off they are also
 * parked (hard disks), stopped (USB) or shut down (NVMe), so they do not count it as a power
 * failure. A drive that does not answer only costs its timeout. */
void blk_shutdown(int off) {
    for (int i = 0; i < nb; i++) {
        blkdev *d = &bd[i];
        if (d->gone)
            continue;
        if (d->fl)
            d->fl(d);
        if (off && d->off)
            d->off(d);
    }
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
        c += !bd[i].gone && !bd[i].cd;
    return c;
}
blkdev *blk_get(int n) {
    for (int i = 0; i < nb; i++)
        if (!bd[i].gone && !bd[i].cd && n-- == 0)
            return &bd[i];
    return 0;
}
int blk_count_all(void) {
    int c = 0;
    for (int i = 0; i < nb; i++)
        c += !bd[i].gone;
    return c;
}
blkdev *blk_get_all(int n) {
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
/* STANDBY IMMEDIATE: the disk parks its heads and spins down */
static int ide_off(blkdev *x) {
    ide_t *d = x->p;
    sel(d, 0);
    if (wait_nbsy(d->base) >= 0) {
        outb(d->base + 7, 0xE0);
        wait_nbsy(d->base);
    }
    return 0;
}
/* ATAPI (CD/DVD drives on IDE): SCSI commands as 12-byte packets, data by PIO */
typedef struct {
    ide_t io;
    cdrive c;
} atapi_t;
static int wait_atapi(u16 b) { /* a drive that spins up takes longer than a disk */
    for (u32 i = 0; i < 10000000; i++) {
        u8 s = inb(b + 7);
        if (s == 0xFF)
            return -1;
        if (!(s & 0x80))
            return s;
    }
    return -1;
}
static int atapi_pkt(cdrive *c, const u8 *cdb, void *buf, u32 len) {
    ide_t *d = &((atapi_t *)c->p)->io;
    u16 b = d->base;
    outb(b + 6, 0xA0 | (d->sl << 4));
    for (int i = 0; i < 4; i++)
        inb(d->ctrl);
    if (wait_atapi(b) < 0)
        return -1;
    u32 lim = len > 0xF800 ? 0xF800 : len ? len : 2;
    outb(b + 1, 0); /* PIO */
    outb(b + 4, (u8)lim);
    outb(b + 5, (u8)(lim >> 8));
    outb(b + 7, 0xA0);
    for (int i = 0; i < 4; i++)
        inb(d->ctrl);
    int s = wait_atapi(b);
    if (s < 0 || (s & 1) || !(s & 8))
        return -1;
    for (int i = 0; i < 6; i++)
        outw(b, (u16)(cdb[i * 2] | cdb[i * 2 + 1] << 8));
    u8 *o = buf;
    u32 got = 0;
    for (int rounds = 0; rounds < 4096; rounds++) {
        for (int i = 0; i < 4; i++)
            inb(d->ctrl);
        s = wait_atapi(b);
        if (s < 0 || (s & 1))
            return -1;
        if (!(s & 8))
            return 0;
        u32 n = inb(b + 4) | (u32)inb(b + 5) << 8;
        if (!n || n > 0xFFFF)
            return -1;
        for (u32 i = 0; i < n; i += 2) {
            u16 w = inw(b);
            if (got < len)
                o[got] = (u8)w;
            if (got + 1 < len)
                o[got + 1] = (u8)(w >> 8);
            got += 2;
        }
    }
    return -1;
}
static void atapi_one(u16 base, u16 ctrl, u8 sl) {
    outb(base + 6, 0xA0 | (sl << 4));
    for (int i = 0; i < 4; i++)
        inb(ctrl);
    outb(base + 7, 0xA1); /* IDENTIFY PACKET DEVICE */
    for (int i = 0; i < 15; i++)
        inb(ctrl);
    if (wait_drq(base))
        return;
    static u16 w[256];
    for (int i = 0; i < 256; i++)
        w[i] = inw(base);
    if (((w[0] >> 8) & 0x1F) != 5)
        return; /* not a CD/DVD drive */
    atapi_t *a = kmalloc(sizeof *a);
    if (!a)
        return;
    memset(a, 0, sizeof *a);
    a->io.base = base;
    a->io.ctrl = ctrl;
    a->io.sl = sl;
    a->c.pkt = atapi_pkt;
    a->c.p = a;
    for (int i = 0; i < 20; i++) {
        a->c.model[i * 2] = (char)(w[27 + i] >> 8);
        a->c.model[i * 2 + 1] = (char)w[27 + i];
    }
    for (int i = 39; i >= 0 && (a->c.model[i] == ' ' || !a->c.model[i]); i--)
        a->c.model[i] = 0;
    cd_add(&a->c);
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
    if (s == 0xFF)
        return;
    for (u32 i = 0; i < 500000 && (inb(base + 7) & 0x80); i++)
        ;
    /* a CD drive answers with this signature, its status may well be 0 */
    if (inb(base + 4) == 0x14 && inb(base + 5) == 0xEB) {
        atapi_one(base, ctrl, sl); /* a CD or DVD drive */
        return;
    }
    if (!s)
        return;
    outb(base + 2, 0);
    outb(base + 3, 0);
    outb(base + 4, 0);
    outb(base + 5, 0);
    outb(base + 7, 0xEC);
    for (int i = 0; i < 15; i++)
        inb(ctrl);
    if (inb(base + 4) == 0x14 && inb(base + 5) == 0xEB) { /* IDENTIFY refused: ATAPI */
        atapi_one(base, ctrl, sl);
        return;
    }
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
    blkdev *b = blk_register("IDE", sec, d, ide_rd, ide_wr, ide_fl);
    if (b) {
        b->off = ide_off;
        blk_model_ata(b, w);
    }
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
/* With several Violyx on one disk (next to other systems), the data that belongs to the started
 * one: the Violyx partition right behind the boot partition the kernel was loaded from */
static int probe(blkdev *d) {
    static u8 s[512];
    if (d->rd(d, 1, s) || memcmp(s, "EFI PART", 8))
        return 0;
    u64 el = *(u64 *)(s + 72);
    u32 cnt = *(u32 *)(s + 80), esz = *(u32 *)(s + 84);
    if (esz < 128 || esz > 512 || cnt > 512)
        return 0;
    const u8 *bp = boot_drive(d) ? boot_part_guid() : 0;
    u64 bend = 0, lba = 0, last = 0;
    int found = 0;
    for (int pass = 0; pass < 2; pass++)
        for (u32 i = 0; i < cnt; i++) {
            if (d->rd(d, el + i * esz / 512, s))
                return 0;
            u8 *e = s + (i * esz) % 512;
            u64 a = *(u64 *)(e + 32), z = *(u64 *)(e + 40);
            if (!pass) {
                if (bp && !memcmp(e + 16, bp, 16))
                    bend = z + 1;
                continue;
            }
            if (memcmp(e, GUID, 16) || z < a)
                continue;
            /* the first one, or the nearest behind the boot partition */
            if (!found || (bend && (lba < bend || a < lba) && a >= bend)) {
                lba = a;
                last = z;
                found = 1;
            }
        }
    if (!found)
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
static char stbuf[48];
int fs_init(void) {
    if (!nb) {
        SETST("kein beschreibbarer Datenträger", "no writable storage");
        return -1;
    }
    int old = 0;
    /* first the disk the system was started from: with Violyx on several disks it is that one */
    for (int i = -1; i < nb; i++) {
        blkdev *d = i < 0 ? 0 : &bd[i];
        if (i < 0) {
            for (int k = 0; k < nb && !d; k++)
                if (!bd[k].gone && !bd[k].cd && boot_drive(&bd[k]))
                    d = &bd[k];
            if (!d)
                continue;
        } else if (bd[i].gone || bd[i].cd)
            continue;
        int r = probe(d);
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
