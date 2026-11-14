#include "os.h"
#include "cdda.h"
/* CD and DVD drives over IDE, SATA (AHCI) or USB. The transports only send SCSI commands; here
 * the disc is looked for about every two seconds, and while one is in, it is a read-only drive
 * with 512-byte sectors like every other (a CD has 2048-byte blocks: four sectors each). */
#define MAXCD 4
#define CBLK  16 /* blocks read ahead: 32 KiB */
#define ABLK  13 /* audio sectors read ahead (2352 bytes each): what fits into 32 KiB */
static cdrive *cds[MAXCD];
void cd_add(cdrive *c) {
    for (int i = 0; i < MAXCD; i++)
        if (!cds[i]) {
            c->cbuf = kmalloc(CBLK * 2048 > ABLK * 2352 ? CBLK * 2048 : ABLK * 2352);
            c->ccnt = 0;
            c->next = 0;
            cds[i] = c;
            return;
        }
}
void cd_remove(cdrive *c) {
    for (int i = 0; i < MAXCD; i++)
        if (cds[i] == c)
            cds[i] = 0;
    if (c->dev) {
        blk_gone(c->dev);
        c->dev = 0;
        drives_sync();
    }
    kfree(c->a);
    c->a = 0;
    kfree(c->abuf);
    c->abuf = 0;
    c->acnt = 0;
    c->ccnt = 0;
    kfree(c->cbuf);
    c->cbuf = 0;
}
static int cd_ready(cdrive *c);
static int cmd(cdrive *c, u8 op, u32 a, u32 b, void *buf, u32 len) {
    u8 k[12] = {op};
    switch (op) {
        case 0x28: /* READ(10): a = block, b = count */
            k[2] = (u8)(a >> 24);
            k[3] = (u8)(a >> 16);
            k[4] = (u8)(a >> 8);
            k[5] = (u8)a;
            k[7] = (u8)(b >> 8);
            k[8] = (u8)b;
            break;
        case 0x03: /* REQUEST SENSE */
            k[4] = (u8)len;
            break;
        case 0x1B: /* START STOP UNIT: a = 2 opens the tray */
            k[4] = (u8)a;
            break;
        case 0x43: /* READ TOC: a = format (0 tracks, 5 CD-Text) */
            k[2] = (u8)a;
            k[7] = (u8)(len >> 8);
            k[8] = (u8)len;
            break;
        case 0xBE: /* READ CD: a = sector, b = count; CD-DA, only the 2352 bytes of audio */
            k[1] = 1 << 2;
            k[2] = (u8)(a >> 24);
            k[3] = (u8)(a >> 16);
            k[4] = (u8)(a >> 8);
            k[5] = (u8)a;
            k[6] = (u8)(b >> 16);
            k[7] = (u8)(b >> 8);
            k[8] = (u8)b;
            k[9] = 0x10;
            break;
    }
    return c->pkt(c, k, buf, len);
}
/* blocks b .. b+n-1 (n <= CBLK) through the read-ahead buffer */
static const u8 *cd_block(cdrive *c, u32 b) {
    if (c->ccnt && b >= c->cblk && b < c->cblk + c->ccnt)
        return c->cbuf + (b - c->cblk) * 2048;
    if (!c->cbuf || !c->dev)
        return 0;
    u64 last = c->dev->sectors / 4;
    u32 n = CBLK;
    if (b >= last)
        return 0;
    if (b + n > last)
        n = (u32)(last - b);
    c->ccnt = 0;
    if (cmd(c, 0x28, b, n, c->cbuf, n * 2048)) {
        /* one block alone: a damaged spot further on should not cost this one */
        if (n == 1 || cmd(c, 0x28, b, 1, c->cbuf, 2048))
            return 0;
        n = 1;
    }
    c->cblk = b;
    c->ccnt = n;
    return c->cbuf;
}
static int cd_rd(blkdev *d, u64 l, void *buf) {
    cdrive *c = d->p;
    if (c->a)
        return -1; /* an audio CD has no data blocks */
    const u8 *s = cd_block(c, (u32)(l / 4));
    if (!s)
        return -1;
    memcpy(buf, s + (l % 4) * 512, 512);
    return 0;
}
static int cd_rdn(blkdev *d, u64 l, u32 n, void *buf) {
    cdrive *c = d->p;
    if (c->a)
        return -1;
    u8 *o = buf;
    while (n) {
        const u8 *s = cd_block(c, (u32)(l / 4));
        if (!s)
            return -1;
        u32 in = (u32)(l % 4);
        /* everything in the buffer from here on */
        u32 have = (c->cblk + c->ccnt - (u32)(l / 4)) * 4 - in, k = have < n ? have : n;
        memcpy(o, s + in * 512, k * 512);
        o += k * 512;
        l += k;
        n -= k;
    }
    return 0;
}
static int cd_wr(blkdev *d, u64 l, const void *b) {
    (void)d, (void)l, (void)b;
    return -1;
}
static int cd_fl(blkdev *d) {
    (void)d;
    return 0;
}
/* Audio. Playing a CD must never wait for the drive, so a window of about ten seconds is read
 * ahead while the computer has nothing else to do (cd_poll, from idle). A program only waits when
 * it jumps somewhere else, or when the drive is slower than the music. */
#define ACACHE 768 /* sectors in the window: 1.7 MB */
/* the next piece of the window from the drive; wait = 1: a program waits for it, so read again
 * and turn what cannot be read into silence. 1 = read, 0 = nothing to do or failed */
static int ar_fill(cdrive *c, int wait) {
    u32 lba = c->ablk + c->acnt;
    if (lba >= c->a->lead || (!wait && lba == c->astop))
        return 0;
    if (c->acnt + ABLK > ACACHE) { /* full: drop what was played, keep a little for going back */
        u32 d = c->apos > c->ablk + 32 ? c->apos - c->ablk - 32 : 0;
        if (d > c->acnt)
            d = c->acnt;
        if (!d)
            return 0;
        memmove(c->abuf, c->abuf + (u64)d * 2352, (u64)(c->acnt - d) * 2352);
        c->ablk += d;
        c->acnt -= d;
    }
    u32 m = ABLK;
    if (m > c->a->lead - lba)
        m = c->a->lead - lba;
    if (m > ACACHE - c->acnt)
        m = ACACHE - c->acnt;
    int ok = 0;
    for (int t = 0; t < (wait ? 3 : 1) && !ok; t++)
        ok = !cmd(c, 0xBE, lba, m, c->cbuf, m * 2352);
    if (!ok && !wait) {
        c->astop = lba; /* a scratch: left to the program, which reads it with retries */
        return 0;
    }
    if (!ok) { /* sector by sector: a scratch should cost a crackle, not the whole track */
        m = 1;
        for (int t = 0; t < 3 && !ok; t++)
            ok = !cmd(c, 0xBE, lba, 1, c->cbuf, 2352);
        if (!ok) {
            int r = cd_ready(c);
            if (r == 1 || r == 2)
                return 0; /* the disc is gone */
            memset(c->cbuf, 0, 2352);
            c->errs++;
        }
    }
    memcpy(c->abuf + (u64)c->acnt * 2352, c->cbuf, (u64)m * 2352);
    c->acnt += m;
    return 1;
}
static int cd_audio(void *ctx, u32 lba, u32 n, void *buf) {
    cdrive *c = ctx;
    u8 *o = buf;
    if (!c->cbuf || !c->a)
        return -1;
    if (!c->abuf) {
        c->abuf = kmalloc((u64)ACACHE * 2352);
        c->acnt = 0;
        if (!c->abuf)
            return -1;
    }
    c->aseen = ticks;
    for (u32 k = 0; k < n; k++, lba++, o += 2352) {
        if (lba >= c->a->lead)
            return -1;
        c->apos = lba;
        if (!(lba >= c->ablk && lba < c->ablk + c->acnt)) {
            if (c->dev && c->dev->gone)
                return -1;
            if (!(c->acnt && lba == c->ablk + c->acnt)) { /* a jump: start a new window */
                c->ablk = lba;
                c->acnt = 0;
            }
            if (!ar_fill(c, 1))
                return -1;
        }
        memcpy(o, c->abuf + (u64)(lba - c->ablk) * 2352, 2352);
    }
    return 0;
}
static int cd_eject(blkdev *d) {
    cdrive *c = d->p;
    int r = cmd(c, 0x1B, 2, 0, 0, 0);
    c->next = 0; /* look again soon: the disc is gone */
    return r;
}
/* 0 ready, 1 no disc (or not yet), 2 changed, -1 no answer */
static int cd_ready(cdrive *c) {
    u8 s[18];
    if (!cmd(c, 0x00, 0, 0, 0, 0))
        return 0;
    memset(s, 0, sizeof s);
    if (cmd(c, 0x03, 0, 0, s, 18))
        return -1;
    int key = s[2] & 15, asc = s[12];
    return key == 6 && asc == 0x28 ? 2 : 1;
}
/* An audio CD: tracks from the table of contents, titles from CD-Text */
static int cd_attach_audio(cdrive *c) {
    static u8 t[4 + 100 * 8];
    memset(t, 0, sizeof t);
    if (cmd(c, 0x43, 0, 0, t, sizeof t))
        return 0;
    cdda *a = kmalloc(sizeof *a);
    if (!a)
        return 0;
    if (cda_toc(a, t, sizeof t) || a->isdata[a->first]) {
        kfree(a); /* a data disc (or one that starts with data): ISO 9660 */
        return 0;
    }
    u8 *x = kmalloc(32768);
    if (x) {
        memset(x, 0, 4);
        if (!cmd(c, 0x43, 5, 0, x, 32768))
            cda_text(a, x, 32768);
        kfree(x);
    }
    a->rd = cd_audio;
    a->ctx = c;
    c->acnt = 0;
    c->astop = ~0u;
    c->ccnt = 0;
    c->errs = 0;
    kfree(c->a);
    c->a = a;
    c->dev = blk_register(c->usb ? "USB-CD" : "CD", (u64)a->lead * 4, c, cd_rd, cd_wr, cd_fl);
    if (!c->dev)
        return 1;
    c->dev->cd = 1;
    c->dev->usb = c->usb;
    c->dev->eject = cd_eject;
    c->dev->audio = a;
    strlcpy_(c->dev->model, c->model[0] ? c->model : "CD/DVD", 41);
    return 1;
}
static void cd_attach(cdrive *c) {
    u8 b[8];
    if (cd_attach_audio(c))
        return;
    if (cmd(c, 0x25, 0, 0, b, 8))
        return;
    u32 last = (u32)b[0] << 24 | (u32)b[1] << 16 | (u32)b[2] << 8 | b[3],
        bs = (u32)b[4] << 24 | (u32)b[5] << 16 | (u32)b[6] << 8 | b[7];
    if (bs != 2048 || !last)
        return; /* audio CDs have no data blocks to read */
    c->ccnt = 0;
    c->dev = blk_register(c->usb ? "USB-CD" : "CD", ((u64)last + 1) * 4, c, cd_rd, cd_wr, cd_fl);
    if (!c->dev)
        return;
    c->dev->cd = 1;
    c->dev->usb = c->usb;
    c->dev->rdn = cd_rdn;
    c->dev->eject = cd_eject;
    strlcpy_(c->dev->model, c->model[0] ? c->model : "CD/DVD", 41);
}
void cd_poll(void) {
    int changed = 0;
    for (int i = 0; i < MAXCD; i++) {
        cdrive *c = cds[i];
        if (!c)
            continue;
        /* read ahead while a program plays the CD (it read in the last five seconds) */
        if (c->a && c->abuf && c->acnt && ticks - c->aseen < 500 &&
            c->ablk + c->acnt < c->apos + ACACHE - 2 * ABLK)
            ar_fill(c, 0);
        if (ticks < c->next)
            continue;
        int r = cd_ready(c);
        c->dead = r < 0 ? c->dead + 1 : 0;
        /* a drive that does not answer is asked less often, but never given up: some take
         * many seconds after power-on or after a disc went in */
        c->next = ticks + (c->dead >= 5 ? 1000 : 200);
        if (c->dev && r > 0) { /* taken out or changed */
            blk_gone(c->dev);
            c->dev = 0;
            c->ccnt = 0;
            kfree(c->a); /* drives_sync below forgets the drive before anyone reads again */
            c->a = 0;
            c->acnt = 0;
            c->next = ticks + 50;
            changed = 1;
        } else if (!c->dev && !r) {
            cd_attach(c);
            changed |= c->dev != 0;
        }
    }
    if (changed)
        drives_sync();
}
#ifdef FAKECD
/* Test build only (make EXTRA=-DFAKECD): an audio CD drive in memory with three tracks, CD-Text
 * and one sector that cannot be read. QEMU has no audio CDs. */
static const u32 fk_st[4] = {0, 40 * 75, 60 * 75, 85 * 75};
static int fk_pkt(cdrive *c, const u8 *k, void *buf, u32 len) {
    (void)c;
    u8 *o = buf;
    if (k[0] == 0x00 || k[0] == 0x1B)
        return 0;
    if (k[0] == 0x03) {
        memset(o, 0, len);
        return 0;
    }
    if (k[0] == 0x43 && k[2] == 0) {
        u8 t[4 + 4 * 8] = {0, 2 + 4 * 8, 1, 3};
        for (int i = 0; i < 4; i++) {
            u8 *d = t + 4 + i * 8;
            d[1] = 0x10;
            d[2] = i == 3 ? 0xAA : (u8)(i + 1);
            d[6] = (u8)(fk_st[i] >> 8);
            d[7] = (u8)fk_st[i];
        }
        memset(o, 0, len);
        memcpy(o, t, len < sizeof t ? len : sizeof t);
        return 0;
    }
    if (k[0] == 0x43 && k[2] == 5) {
        static const char *tt = "Testalbum Gr\xfc\xdf"
                                "e\0Erster Titel\0Zweiter: Teil\0Dritter\0";
        static const char *ar = "Violyx Band\0\t\0\t\0\t\0";
        u32 n = 4;
        memset(o, 0, len);
        for (int ty = 0; ty < 2; ty++) {
            const char *s = ty ? ar : tt;
            u32 l = ty ? 20 : 60, trk = 0;
            for (u32 a = 0; a < l && n + 18 <= len; a += 12, n += 18) {
                o[n] = (u8)(0x80 + ty);
                o[n + 1] = (u8)trk;
                for (u32 j = 0; j < 12; j++)
                    o[n + 4 + j] = a + j < l ? (u8)s[a + j] : 0;
                for (u32 j = 0; j < 12 && a + j < l; j++)
                    if (!s[a + j])
                        trk++;
            }
        }
        o[0] = (u8)((n - 2) >> 8);
        o[1] = (u8)(n - 2);
        return 0;
    }
    if (k[0] == 0xBE) {
        u32 lba = (u32)k[2] << 24 | (u32)k[3] << 16 | (u32)k[4] << 8 | k[5],
            n = (u32)k[6] << 16 | (u32)k[7] << 8 | k[8];
        udelay(20000 + n * 2000); /* about as fast as a real drive */
        if (lba <= 1000 && lba + n > 1000)
            return -1; /* a scratch */
        for (u32 s = 0; s < n && (s + 1) * 2352 <= len; s++) {
            u32 l = lba + s, tr = l >= fk_st[2] ? 2 : l >= fk_st[1] ? 1 : 0;
            for (u32 i = 0; i < 588; i++) {
                u32 t = (l * 588 + i);
                /* a triangle wave: 220, 330 or 440 Hz on the left, a fifth higher on the right */
                u32 pl = t * (tr + 2) * 110 % 44100, pr = t * (tr + 2) * 165 % 44100;
                int vl = (int)(pl < 22050 ? pl : 44100 - pl) - 11025,
                    vr = (int)(pr < 22050 ? pr : 44100 - pr) - 11025;
                short *q = (short *)(o + s * 2352 + i * 4);
                q[0] = (short)(vl * 2);
                q[1] = (short)(vr * 2);
            }
        }
        return 0;
    }
    return -1;
}
void cd_fake(void) {
    static cdrive c;
    c.pkt = fk_pkt;
    c.usb = 1;
    strlcpy_(c.model, "Test-CD", 41);
    cd_add(&c);
}
#endif
