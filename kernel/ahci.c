#include "os.h"
/* AHCI (SATA): one command at a time, polled instead of interrupts */
#define HR(o) (*(volatile u32 *)(base + (o)))
#define PR(o) (*(volatile u32 *)(pb + (o)))
typedef struct {
    u64 pb;
    u32 *cl;
    u8 *tb;
    u8 *buf;
} port;
static const int WAIT = 5000000;
static int issue(port *p, u8 cmd, u64 lba, int data, int wr) {
    u64 pb = p->pb;
    u32 *h = p->cl;
    u8 *f = p->tb;
    memset(f, 0, 128);
    f[0] = 0x27;
    f[1] = 0x80;
    f[2] = cmd;
    f[4] = lba;
    f[5] = lba >> 8;
    f[6] = lba >> 16;
    f[7] = 0x40;
    f[8] = lba >> 24;
    f[9] = lba >> 32;
    f[10] = lba >> 40;
    f[12] = data ? 1 : 0;
    h[0] = 5 | (wr ? 1 << 6 : 0) | ((data ? 1 : 0) << 16);
    h[1] = 0;
    u64 t = phys(p->tb);
    h[2] = t;
    h[3] = t >> 32;
    u32 *pr = (u32 *)(p->tb + 128);
    u64 b = phys(p->buf);
    pr[0] = b;
    pr[1] = b >> 32;
    pr[2] = 0;
    pr[3] = 511;
    PR(0x10) = PR(0x10);
    PR(0x38) = 1;
    for (int i = 0; i < WAIT; i++) {
        u32 tf = PR(0x20), is = PR(0x10);
        if (is & (1u << 30) || tf & 1)
            return -1;
        if (!(PR(0x38) & 1))
            return 0;
        udelay(1);
    }
    return -1;
}
static int a_rd(blkdev *d, u64 l, void *b) {
    port *p = d->p;
    if (issue(p, 0x25, l, 1, 0))
        return -1;
    memcpy(b, p->buf, 512);
    return 0;
}
static int a_wr(blkdev *d, u64 l, const void *b) {
    port *p = d->p;
    memcpy(p->buf, b, 512);
    return issue(p, 0x35, l, 1, 1);
}
static int a_fl(blkdev *d) {
    return issue(d->p, 0xEA, 0, 0, 0);
}
void ahci_init(const pci_t *c) {
    pci_enable(c);
    u64 base = pci_bar(c, 5);
    if (!base)
        return;
    mmio_map(base, 0x2000);
    if (HR(0x24) & 1) {
        HR(0x28) |= 2;
        for (int i = 0; i < 100000 && (HR(0x28) & 1); i++)
            udelay(10);
    }
    HR(4) |= 0x80000000u;
    u32 pi = HR(0xC);
    for (int n = 0; n < 32; n++) {
        if (!(pi >> n & 1))
            continue;
        u64 pb = base + 0x100 + n * 0x80;
        if ((PR(0x28) & 15) != 3 || PR(0x24) != 0x101)
            continue;
        PR(0x18) &= ~1u;
        for (int i = 0; i < 500000 && (PR(0x18) & (1 << 15)); i++)
            udelay(1);
        PR(0x18) &= ~0x10u;
        for (int i = 0; i < 500000 && (PR(0x18) & (1 << 14)); i++)
            udelay(1);
        port *p = dma_alloc(sizeof *p);
        p->pb = pb;
        p->cl = dma_alloc(1024);
        u8 *fis = dma_alloc(256);
        p->tb = dma_alloc(256);
        p->buf = dma_alloc(512);
        u64 a = phys(p->cl);
        PR(0) = a;
        PR(4) = a >> 32;
        a = phys(fis);
        PR(8) = a;
        PR(12) = a >> 32;
        PR(0x18) |= 0x10;
        PR(0x30) = ~0u;
        PR(0x10) = ~0u;
        for (int i = 0; i < 1000000 && (PR(0x20) & 0x88); i++)
            udelay(1);
        PR(0x18) |= 1;
        if (issue(p, 0xEC, 0, 1, 0) == 0) {
            u16 *w = (u16 *)p->buf;
            u64 s = *(u64 *)(w + 100);
            blk_register("AHCI", s, p, a_rd, a_wr, a_fl);
            blk_model_ata(blk_get(blk_count() - 1), w);
        }
    }
}
