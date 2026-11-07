#include "os.h"
/* NVMe: one admin and one I/O queue, polled */
#define R32(o) (*(volatile u32 *)(n->base + (o)))
#define R64(o) (*(volatile u64 *)(n->base + (o)))
#define QS     16
typedef struct {
    u32 *sq;
    u32 *cq;
    int t, h, ph, id;
} q_t;
typedef struct {
    u64 base;
    u32 stride;
    q_t a, io;
    u8 *buf;
} nv;
static void *sqn(int n) {
    return dma_alloc(n * 64);
}
static int submit(nv *n, q_t *q, u32 *c) {
    memcpy(q->sq + q->t * 16, c, 64);
    q->t = (q->t + 1) % QS;
    R32(0x1000 + 2 * q->id * n->stride) = q->t;
    for (int i = 0; i < 5000000; i++) {
        volatile u32 *e = q->cq + q->h * 4;
        u32 st = e[3];
        if (((st >> 16) & 1) == (u32)q->ph) {
            q->h = (q->h + 1) % QS;
            if (!q->h)
                q->ph ^= 1;
            R32(0x1000 + (2 * q->id + 1) * n->stride) = q->h;
            return (st >> 17) & 0x7FFF ? -1 : 0;
        }
        udelay(1);
    }
    return -1;
}
static int ioc(nv *n, u8 op, u64 lba) {
    u32 c[16] = {0};
    u64 p = phys(n->buf);
    c[0] = op | (7 << 16);
    c[1] = 1;
    c[6] = p;
    c[7] = p >> 32;
    c[10] = lba;
    c[11] = lba >> 32;
    return submit(n, &n->io, c);
}
static int n_rd(blkdev *d, u64 l, void *b) {
    nv *n = d->p;
    if (ioc(n, 2, l))
        return -1;
    memcpy(b, n->buf, 512);
    return 0;
}
static int n_wr(blkdev *d, u64 l, const void *b) {
    nv *n = d->p;
    memcpy(n->buf, b, 512);
    return ioc(n, 1, l);
}
static int n_fl(blkdev *d) {
    nv *n = d->p;
    u32 c[16] = {0};
    c[0] = 0 | (8 << 16);
    c[1] = 1;
    return submit(n, &n->io, c);
}
void nvme_init(const pci_t *c) {
    pci_enable(c);
    u64 bar = pci_bar(c, 0);
    if (!bar)
        return;
    mmio_map(bar, 0x4000);
    nv *n = dma_alloc(sizeof *n);
    n->base = bar;
    u64 cap = R64(0);
    n->stride = 4 << ((cap >> 32) & 15);
    if ((cap >> 48 & 15) != 0)
        return;
    u32 to = ((cap >> 24) & 255) * 50000 + 10000;
    R32(0x14) &= ~1u;
    for (u32 i = 0; i < to && (R32(0x1C) & 1); i++)
        udelay(10);
    n->a = (q_t){sqn(QS), dma_alloc(QS * 16), 0, 0, 1, 0};
    n->buf = dma_alloc(4096);
    R32(0x24) = ((QS - 1) << 16) | (QS - 1);
    R64(0x28) = phys(n->a.sq);
    R64(0x30) = phys(n->a.cq);
    R32(0x14) = 1 | (0 << 7) | (6 << 16) | (4 << 20);
    for (u32 i = 0; i < to && !(R32(0x1C) & 3); i++)
        udelay(10);
    if ((R32(0x1C) & 3) != 1)
        return;
    u32 k[16] = {0};
    u64 p = phys(n->buf);
    k[0] = 6 | (1 << 16);
    k[1] = 1;
    k[6] = p;
    k[7] = p >> 32;
    k[10] = 0;
    if (submit(n, &n->a, k))
        return;
    u64 sz = *(u64 *)n->buf;
    u8 fl = n->buf[26] & 15;
    u8 ds = n->buf[128 + fl * 4 + 2];
    if (!sz || ds != 9)
        return; /* 512-byte sectors only for now */
    n->io = (q_t){sqn(QS), dma_alloc(QS * 16), 0, 0, 1, 1};
    memset(k, 0, 64);
    k[0] = 5 | (2 << 16);
    p = phys(n->io.cq);
    k[6] = p;
    k[7] = p >> 32;
    k[10] = ((QS - 1) << 16) | 1;
    k[11] = 1;
    if (submit(n, &n->a, k))
        return;
    memset(k, 0, 64);
    k[0] = 1 | (3 << 16);
    p = phys(n->io.sq);
    k[6] = p;
    k[7] = p >> 32;
    k[10] = ((QS - 1) << 16) | 1;
    k[11] = (1 << 16) | 1;
    if (submit(n, &n->a, k))
        return;
    blk_register("NVMe", sz, n, n_rd, n_wr, n_fl);
    memset(k, 0, 64);
    k[0] = 6 | (4 << 16);
    p = phys(n->buf);
    k[6] = p;
    k[7] = p >> 32;
    k[10] = 1;
    if (!submit(n, &n->a, k)) {
        blkdev *d = blk_get(blk_count() - 1);
        memcpy(d->model, n->buf + 24, 40);
        d->model[40] = 0;
        for (int i = 39; i >= 0 && d->model[i] == ' '; i--)
            d->model[i] = 0;
    }
}
