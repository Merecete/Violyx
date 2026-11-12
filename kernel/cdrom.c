#include "os.h"
/* CD and DVD drives over IDE, SATA (AHCI) or USB. The transports only send SCSI commands; here
 * the disc is looked for about every two seconds, and while one is in, it is a read-only drive
 * with 512-byte sectors like every other (a CD has 2048-byte blocks: four sectors each). */
#define MAXCD 4
#define CBLK  16 /* blocks read ahead: 32 KiB */
static cdrive *cds[MAXCD];
void cd_add(cdrive *c) {
    for (int i = 0; i < MAXCD; i++)
        if (!cds[i]) {
            c->cbuf = kmalloc(CBLK * 2048);
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
    c->ccnt = 0;
    kfree(c->cbuf);
    c->cbuf = 0;
}
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
    const u8 *s = cd_block(c, (u32)(l / 4));
    if (!s)
        return -1;
    memcpy(buf, s + (l % 4) * 512, 512);
    return 0;
}
static int cd_rdn(blkdev *d, u64 l, u32 n, void *buf) {
    cdrive *c = d->p;
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
static void cd_attach(cdrive *c) {
    u8 b[8];
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
        if (!c || ticks < c->next || c->dead >= 5)
            continue;
        c->next = ticks + 200;
        int r = cd_ready(c);
        c->dead = r < 0 ? c->dead + 1 : 0;
        if (c->dev && r > 0) { /* taken out or changed */
            blk_gone(c->dev);
            c->dev = 0;
            c->ccnt = 0;
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
