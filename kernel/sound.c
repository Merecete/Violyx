#include "os.h"
/* Sound: Intel HD Audio (real PCs, QEMU, VirtualBox), AC'97 (VirtualBox) and USB audio. All play
 * the same ring buffer. A small mixer in the timer creates the tones: 48 kHz, 16 bit, stereo, no
 * files and no firmware. */
#define RATE   48000
#define FRAMES 16384
#define LAT    3000
#define VOICES 12
enum {
    S_NONE,
    S_HDA,
    S_AC97,
    S_USB
};
typedef struct {
    u32 ph, inc, lf;
    int left, total, delay, vol, wave;
} voice;
static voice vo[VOICES];
static short *ring;
static int kind;
static u32 wp, silent;
static int fading, fgain = 256; /* shutdown: the sound fades out within a quarter second */
int cfg_vol = 7;
void snd_tone(int freq, int ms, int vol, int wave, int delay) {
    if (!kind || fading || !cfg_vol || freq < 20 || freq > 16000 || ms <= 0)
        return;
    if (ms > 4000)
        ms = 4000;
    if (vol < 1)
        return;
    if (vol > 100)
        vol = 100;
    if (delay < 0)
        delay = 0;
    if (delay > 10000)
        delay = 10000;
    u64 f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f));
    voice *v = &vo[0];
    for (int i = 0; i < VOICES; i++) {
        if (!vo[i].left) {
            v = &vo[i];
            break;
        }
        if (vo[i].left + vo[i].delay < v->left + v->delay)
            v = &vo[i];
    }
    v->ph = 0;
    v->inc = (u32)(((u64)freq << 32) / RATE);
    v->left = v->total = ms * (RATE / 1000);
    v->delay = delay * (RATE / 1000);
    v->vol = vol * 90;
    v->wave = wave;
    v->lf = 0xACE1u ^ (u32)freq;
    if (f & 0x200)
        __asm__ volatile("sti");
}
static int sample(voice *v) {
    int s;
    u32 p = v->ph;
    v->ph += v->inc;
    switch (v->wave) {
        case 1: {
            int t = (int)(p >> 16);
            s = t < 32768 ? t * 2 - 32768 : (65535 - t) * 2 - 32767;
            break;
        }
        case 2:
            s = (int)(p >> 16) - 32768;
            break;
        case 3: {
            if ((p ^ v->ph) & 0x80000000u || p > v->ph) {
                u32 b = (v->lf ^ (v->lf >> 2) ^ (v->lf >> 3) ^ (v->lf >> 5)) & 1;
                v->lf = (v->lf >> 1) | (b << 15);
            }
            s = (v->lf & 1) ? 26000 : -26000;
            break;
        }
        default:
            s = (p & 0x80000000u) ? -22000 : 22000;
    }
    int at = v->total - v->left, rel = v->left, e = 256;
    if (at < 96)
        e = at * 256 / 96;
    int r = v->total / 3;
    if (r > 2400)
        r = 2400;
    if (rel < r)
        e = e * rel / r;
    return (int)(((long)s * v->vol / 9000 * e) >> 8);
}
static void mix(u32, u32);
/* Music stream of a program: stereo, 48 kHz. Volume and pause act here, so they take effect at
 * once. */
#define SF 131072
static short *sf;
static u32 sh, st;
static int sgl = 256, sgr = 256, spause;
static u64 sdone;
static inline u32 s_used(void) {
    return (st + SF - sh) % SF;
}
long snd_stream(const short *src, u32 n) {
    if (!kind)
        return -1;
    if (!sf) {
        sf = kmalloc(SF * 4);
        if (!sf)
            return -1;
    }
    u32 room = SF - 1 - s_used();
    if (!src)
        return room;
    if (n > room)
        n = room;
    for (u32 i = 0; i < n; i++) {
        sf[st * 2] = src[i * 2];
        sf[st * 2 + 1] = src[i * 2 + 1];
        st = (st + 1) % SF;
    }
    return n;
}
void snd_stream_flush(void) {
    u64 f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f));
    sh = st;
    if (f & 0x200)
        __asm__ volatile("sti");
}
void snd_fade(void) {
    fading = 1;
}
u64 snd_stream_pos(void) {
    return sdone;
}
void snd_stream_gain(int l, int r) {
    sgl = l;
    sgr = r;
}
void snd_stream_pause(int p) {
    spause = p;
}
static void mix(u32 from, u32 n) {
    short *d = ring + from * 2;
    int act = 0;
    for (int i = 0; i < VOICES; i++)
        act |= vo[i].left;
    int str = sf && sh != st && !spause;
    if ((!act && !str) || !fgain) {
        if (silent >= FRAMES)
            return;
        for (u32 i = 0; i < n * 2; i++)
            d[i] = 0;
        silent += n;
        return;
    }
    silent = 0;
    int g = cfg_vol;
    for (u32 i = 0; i < n; i++) {
        int m = 0;
        for (int k = 0; k < VOICES; k++) {
            voice *v = &vo[k];
            if (!v->left)
                continue;
            if (v->delay) {
                v->delay--;
                continue;
            }
            m += sample(v);
            v->left--;
        }
        int l = m, r = m;
        if (str && sh != st) {
            l += sf[sh * 2] * sgl / 256;
            r += sf[sh * 2 + 1] * sgr / 256;
            sh = (sh + 1) % SF;
            sdone++;
        }
        if (fading && fgain && !(i & 31))
            fgain--;
        l = l * g / 10 * fgain / 256;
        r = r * g / 10 * fgain / 256;
        if (l > 32000)
            l = 32000;
        if (l < -32000)
            l = -32000;
        if (r > 32000)
            r = 32000;
        if (r < -32000)
            r = -32000;
        d[i * 2] = (short)l;
        d[i * 2 + 1] = (short)r;
    }
}
/* ---------- Devices: up to 4 sound chips, one plays. Graphics cards provide HDMI, the mainboard
 * the speakers ---------- */
#define ND 4
typedef struct {
    int kind, ana, dig, eld, sd;
    volatile u8 *hb;
    u32 *corb, *rirb;
    u32 rp;
    u16 nam, nab;
    void *abd;
    short *ring;
    u16 ven, did;
    char mon[20];
    u8 pin[8], np, cad[8];
    u32 upos;
    char uname[24];
} sdev;
static sdev dv[ND];
static int nd, ad = -1;
int cfg_out;
static sdev *D;
#define R32(o) (*(volatile u32 *)(D->hb + (o)))
#define R16(o) (*(volatile u16 *)(D->hb + (o)))
#define R8(o)  (*(volatile u8 *)(D->hb + (o)))
static u32 hcmd(int cad, int nid, u32 v) {
    u32 w = (R16(0x48) + 1) & 255;
    D->corb[w] = (u32)cad << 28 | (u32)nid << 20 | v;
    R16(0x48) = (u16)w;
    for (int i = 0; i < 20000; i++) {
        u32 e = R8(0x58);
        if (e != D->rp) {
            D->rp = (D->rp + 1) & 255;
            R8(0x5D) = 5;
            return D->rirb[D->rp * 2];
        }
        udelay(1);
    }
    return 0;
}
#define PARM(c, n, p) hcmd(c, n, 0xF0000 | (p))
static int conns(int c, int n, int *o, int max) {
    u32 l = PARM(c, n, 0x0E);
    int cnt = l & 127, lng = (l >> 7) & 1, k = 0, prev = 0;
    if (lng)
        return 0;
    for (int i = 0; i < cnt && k < max; i += 4) {
        u32 e = hcmd(c, n, 0xF0200 | i);
        for (int j = 0; j < 4 && i + j < cnt && k < max; j++) {
            int v = (e >> (j * 8)) & 255, id = v & 127;
            if (v & 128) {
                for (int x = prev + 1; x <= id && k < max; x++)
                    o[k++] = x;
            } else
                o[k++] = id;
            prev = id;
        }
    }
    return k;
}
static int gain(int c, int n, int afg, int wc, int p) {
    u32 a = (wc & 8) ? PARM(c, n, p) : PARM(c, afg, p);
    return a & 127;
}
static void amp_out(int c, int n, int afg, int wc) {
    if (wc & 4)
        hcmd(c, n, 0x30000 | 0xB000 | gain(c, n, afg, wc, 0x12));
}
/* Walks back from a pin to a converter and puts everything on stream 1. dig: digital converter for
 * HDMI */
static int route(int c, int afg, int n, int d, u32 fmt, int dig) {
    if (d > 6)
        return 0;
    u32 wc = PARM(c, n, 9);
    int t = (wc >> 20) & 15;
    hcmd(c, n, 0x70500);
    if (t == 0) {
        if (!!(wc & (1 << 9)) != dig)
            return 0;
        hcmd(c, n, 0x20000 | fmt);
        hcmd(c, n, 0x70610);
        if (dig) {
            hcmd(c, n, 0x70D01);
            hcmd(c, n, 0x72D01);
        }
        amp_out(c, n, afg, wc);
        return 1;
    }
    if (t != 2 && t != 3 && t != 4)
        return 0;
    int l[32], m = conns(c, n, l, 32);
    for (int i = 0; i < m; i++)
        if (route(c, afg, l[i], d + 1, fmt, dig)) {
            if (t == 2) {
                if (wc & 2)
                    hcmd(c, n, 0x30000 | 0x7000 | (u32)i << 8 | gain(c, n, afg, wc, 0x0D));
            } else if (m > 1 || dig)
                hcmd(c, n, 0x70100 | i);
            amp_out(c, n, afg, wc);
            return 1;
        }
    return 0;
}
/* Audio InfoFrame: tells the display that stereo PCM is coming */
static void infoframe(int c, int n) {
    u8 f[14] = {0x84, 0x01, 0x0A, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    u8 sum = 0;
    for (int i = 0; i < 14; i++)
        sum += f[i];
    f[3] = (u8)(0x100 - sum);
    hcmd(c, n, 0x73000);
    for (int i = 0; i < 14; i++)
        hcmd(c, n, 0x73100 | f[i]);
    hcmd(c, n, 0x732C0);
}
static void eld_name(int c, int n) {
    u32 sz = hcmd(c, n, 0xF2E08) & 0xFF;
    if (sz < 20 || sz > 256)
        return;
    u8 e[64];
    int k = sz < 64 ? sz : 64;
    for (int i = 0; i < k; i++) {
        u32 v = hcmd(c, n, 0xF2F00 | i);
        if (!(v & 0x80000000u))
            return;
        e[i] = (u8)v;
    }
    int mnl = e[4] & 31, o = 0;
    for (int i = 0; i < mnl && 20 + i < k && o < 19; i++)
        if (e[20 + i] >= 32 && e[20 + i] < 127)
            D->mon[o++] = e[20 + i];
    D->mon[o] = 0;
    D->eld = 1;
}
static void codec(int c) {
    u32 sub = PARM(c, 0, 4);
    int st = (sub >> 16) & 255, nn = sub & 255;
    for (int f = st; f < st + nn; f++) {
        if ((PARM(c, f, 5) & 255) != 1)
            continue;
        hcmd(c, f, 0x70500);
        udelay(30000);
        u32 s = PARM(c, f, 4);
        int a = (s >> 16) & 255, b = s & 255;
        for (int n = a; n < a + b; n++) {
            u32 wc = PARM(c, n, 9);
            if (((wc >> 20) & 15) != 4)
                continue;
            u32 pc = PARM(c, n, 0x0C), cfg = hcmd(c, n, 0xF1C00);
            int dev = (cfg >> 20) & 15, con = cfg >> 30;
            if (!(pc & 16) || con == 1)
                continue;
            int hd = (wc & (1 << 9)) && (pc & ((1 << 7) | (1 << 24)));
            if (hd || dev == 4 || dev == 5) {
                if (!(wc & (1 << 9)))
                    continue;
                if (!route(c, f, n, 0, 0x11, 1))
                    continue;
                hcmd(c, n, 0x70740);
                if (D->np < 8) {
                    D->pin[D->np] = (u8)n;
                    D->cad[D->np++] = (u8)c;
                }
                if (pc & ((1 << 7) | (1 << 24))) {
                    infoframe(c, n);
                    u32 se = hcmd(c, n, 0xF0900);
                    if ((se >> 30) & 1)
                        eld_name(c, n);
                }
                D->dig++;
                continue;
            }
            if (dev > 2)
                continue;
            if (!route(c, f, n, 0, 0x11, 0))
                continue;
            hcmd(c, n, 0x70700 | (dev == 2 ? 0xC0 : 0x40));
            if (pc & (1 << 16))
                hcmd(c, n, 0x70C02);
            D->ana++;
        }
    }
}
static void hda_init(const pci_t *p) {
    pci_enable(p);
    u64 bar = pci_bar(p, 0);
    if (!bar || nd >= ND)
        return;
    D = &dv[nd];
    memset(D, 0, sizeof *D);
    mmio_map(bar, 0x4000);
    D->hb = (volatile u8 *)bar;
    R32(8) = 0;
    for (int i = 0; i < 1000 && (R32(8) & 1); i++)
        udelay(100);
    udelay(1000);
    R32(8) = 1;
    for (int i = 0; i < 2000 && !(R32(8) & 1); i++)
        udelay(100);
    if (!(R32(8) & 1))
        return;
    udelay(2000);
    u32 cads = R16(0x0E);
    u32 g = R16(0);
    int iss = (g >> 8) & 15, oss = (g >> 12) & 15;
    if (!cads || !oss)
        return;
    D->corb = dma_alloc(4096);
    D->rirb = dma_alloc(4096);
    R8(0x4C) = 0;
    R8(0x5C) = 0;
    udelay(1000);
    R8(0x4E) = 2;
    R8(0x5E) = 2;
    R32(0x40) = (u32)phys(D->corb);
    R32(0x44) = (u32)(phys(D->corb) >> 32);
    R32(0x50) = (u32)phys(D->rirb);
    R32(0x54) = (u32)(phys(D->rirb) >> 32);
    R16(0x4A) = 0x8000;
    for (int i = 0; i < 1000 && !(R16(0x4A) & 0x8000); i++)
        udelay(10);
    R16(0x4A) = 0;
    for (int i = 0; i < 1000 && (R16(0x4A) & 0x8000); i++)
        udelay(10);
    R16(0x48) = 0;
    R16(0x58) = 0x8000;
    R16(0x5A) = 1;
    D->rp = 0;
    R8(0x5C) = 3;
    R8(0x4C) = 2;
    udelay(1000);
    for (int c = 0; c < 15; c++)
        if (cads & (1u << c))
            codec(c);
    if (!D->ana && !D->dig) {
        R8(0x4C) = 0;
        R8(0x5C) = 0;
        return;
    }
    u32 sd = 0x80 + (u32)iss * 0x20;
    D->sd = (int)sd;
    D->ring = dma_alloc(FRAMES * 4);
    u64 *bdl = dma_alloc(4096);
    bdl[0] = phys(D->ring);
    bdl[1] = FRAMES * 2;
    bdl[2] = phys(D->ring) + FRAMES * 2;
    bdl[3] = FRAMES * 2;
    R8(sd) = 1;
    for (int i = 0; i < 1000 && !(R8(sd) & 1); i++)
        udelay(10);
    R8(sd) = 0;
    for (int i = 0; i < 1000 && (R8(sd) & 1); i++)
        udelay(10);
    R32(sd + 0x18) = (u32)phys(bdl);
    R32(sd + 0x1C) = (u32)(phys(bdl) >> 32);
    R32(sd + 8) = FRAMES * 4;
    R16(sd + 0xC) = 1;
    R16(sd + 0x12) = 0x11;
    R8(sd + 2) = 0x10;
    R8(sd) = 2;
    D->kind = S_HDA;
    D->ven = p->ven;
    D->did = p->did;
    nd++;
}
/* ---------- AC'97 ---------- */
typedef struct __attribute__((packed)) {
    u32 a;
    u16 n, f;
} abd_t;
static void ac97_init(const pci_t *p) {
    if (nd >= ND)
        return;
    pci_enable(p);
    u32 b0 = pci_rd(p, 0x10), b1 = pci_rd(p, 0x14);
    if (!(b0 & 1) || !(b1 & 1))
        return;
    D = &dv[nd];
    memset(D, 0, sizeof *D);
    pci_wr(p, 4, pci_rd(p, 4) | 5);
    u16 nam = (u16)(b0 & ~3u), nab = (u16)(b1 & ~3u);
    D->nam = nam;
    D->nab = nab;
    outl(nab + 0x2C, 2);
    udelay(20000);
    for (int i = 0; i < 1000 && !(inl(nab + 0x30) & 0x100); i++)
        udelay(100);
    outw(nam, 0);
    udelay(1000);
    outw(nam + 2, 0);
    outw(nam + 4, 0);
    outw(nam + 0x18, 0x0808);
    D->ring = dma_alloc(FRAMES * 4);
    abd_t *abd = dma_alloc(4096);
    D->abd = abd;
    for (int i = 0; i < 32; i++) {
        abd[i].a = (u32)phys(D->ring) + (u32)i * 2048;
        abd[i].n = 1024;
        abd[i].f = 0;
    }
    outb(nab + 0x1B, 2);
    for (int i = 0; i < 1000 && (inb(nab + 0x1B) & 2); i++)
        udelay(10);
    outl(nab + 0x10, (u32)phys(abd));
    outb(nab + 0x15, 31);
    outb(nab + 0x1B, 1);
    D->kind = S_AC97;
    D->ana = 1;
    D->ven = p->ven;
    D->did = p->did;
    nd++;
}
void snd_probe(const pci_t *p) {
    if (p->cls != 4)
        return;
    if (p->sub == 3)
        hda_init(p);
    else if (p->sub == 1)
        ac97_init(p);
}
/* Chooses the output: 0 automatic, 1 speakers, 2 HDMI. Automatic only picks HDMI when a display
 * with sound is reported */
void snd_select(void) {
    int best = -1, sc = -1;
    for (int i = 0; i < nd; i++) {
        sdev *d = &dv[i];
        if (!d->kind)
            continue;
        int v = cfg_out == 1   ? (d->ana ? 2 : 0)
                : cfg_out == 2 ? (d->dig ? 2 + d->eld : 0)
                               : (d->kind == S_USB   ? 4
                                  : d->dig && d->eld ? 3
                                  : d->ana           ? 2
                                  : d->dig           ? 1
                                                     : 0);
        if (v > sc) {
            sc = v;
            best = i;
        }
    }
    u64 f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f));
    if (best != ad) {
        if (ad >= 0 && dv[ad].ring)
            memset(dv[ad].ring, 0, FRAMES * 4);
        ad = best;
        wp = 0;
        silent = 0;
    }
    if (best >= 0) {
        ring = dv[best].ring;
        kind = dv[best].kind;
        dv[best].upos = 0;
    } else {
        kind = 0;
        ad = -1;
    }
    if (f & 0x200)
        __asm__ volatile("sti");
}
int snd_add_usb(const char *name) {
    if (nd >= ND)
        return -1;
    sdev *d = &dv[nd];
    memset(d, 0, sizeof *d);
    d->ring = kmalloc(FRAMES * 4);
    if (!d->ring)
        return -1;
    memset(d->ring, 0, FRAMES * 4);
    d->kind = S_USB;
    d->ana = 1;
    strlcpy_(d->uname, name, 24);
    nd++;
    snd_select();
    return nd - 1;
}
void snd_usb_gone(int i) {
    if (i < 0 || i >= nd)
        return;
    u64 f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f));
    dv[i].kind = 0;
    dv[i].ana = 0;
    if (ad == i) {
        ad = -1;
        kind = 0;
    }
    if (f & 0x200)
        __asm__ volatile("sti");
    snd_select();
}
void usb_audio_feed(const short *, u32, u32 *);
void usb_audio_info(void (*)(const char *, void *), void *, int);
int snd_has_hdmi(void) {
    for (int i = 0; i < nd; i++)
        if (dv[i].dig)
            return 1;
    return 0;
}
void snd_report(void (*cb)(const char *, void *), void *a) {
    char l[80];
    int any = 0;
    for (int i = 0; i < nd; i++)
        any |= dv[i].kind != 0;
    if (!any) {
        cb(TR("Keine Soundkarte gefunden", "No sound card found"), a);
        usb_audio_info(cb, a, 1);
        return;
    }
    for (int i = 0; i < nd; i++) {
        sdev *d = &dv[i];
        if (!d->kind)
            continue;
        if (d->kind == S_USB) {
            ksnprintf(l, 80, "%s USB Audio %s  %s", i == ad ? TR("spielt ", "active ") : "       ",
                      d->uname, TR("Lautsprecher", "Speakers"));
            cb(l, a);
            usb_audio_info(cb, a, 0);
            continue;
        }
        const char *mk = d->ven == 0x10DE   ? "NVIDIA"
                         : d->ven == 0x1002 ? "AMD"
                         : d->ven == 0x8086 ? "Intel"
                         : d->ven == 0x10EC ? "Realtek"
                                            : "";
        ksnprintf(l, 80, "%s %s %s %04x:%04x  %s%s%s",
                  i == ad ? TR("spielt ", "active ") : "       ",
                  d->kind == S_HDA ? "HD Audio" : "AC'97", mk, d->ven, d->did,
                  d->ana ? TR("Lautsprecher ", "Speakers ") : "", d->dig ? "HDMI/DP " : "",
                  d->mon[0] ? d->mon : "");
        cb(l, a);
        if (d->kind == S_HDA) {
            D = d;
            u32 p1 = R32(d->sd + 4);
            udelay(40000);
            u32 p2 = R32(d->sd + 4);
            ksnprintf(l, 80,
                      TR("        Datenstrom %s, Status %02x", "        Stream %s, status %02x"),
                      p1 != p2 ? TR("laeuft", "running") : TR("STEHT", "STOPPED"), R8(d->sd + 3));
            cb(l, a);
            for (int k = 0; k < d->np; k++) {
                u32 se = hcmd(d->cad[k], d->pin[k], 0xF0900),
                    ctl = hcmd(d->cad[k], d->pin[k], 0xF0700);
                ksnprintf(l, 80,
                          TR("        HDMI-Pin %d.%02x: Bildschirm %s, ELD %s, an %02x",
                             "        HDMI pin %d.%02x: display %s, ELD %s, on %02x"),
                          d->cad[k], d->pin[k], se >> 31 ? TR("ja", "yes") : TR("nein", "no"),
                          (se >> 30) & 1 ? TR("ja", "yes") : TR("nein", "no"), ctl & 255);
                cb(l, a);
            }
        }
    }
    usb_audio_info(cb, a, 1);
    cb(TR("Ausgang waehlen: Einstellungen, Ausgabe", "Choose the output: Settings, Output"), a);
}
/* From the timer: fills the ring up to a little ahead of the play position */
void snd_tick(void) {
    if (!kind || ad < 0)
        return;
    D = &dv[ad];
    u32 pos;
    if (kind == S_USB)
        pos = D->upos % FRAMES;
    else if (kind == S_HDA)
        pos = (R32(D->sd + 4) / 4) % FRAMES;
    else {
        u16 nab = D->nab;
        u32 civ = inb(nab + 0x14) & 31;
        pos = (civ * 512 + 512 - (inw(nab + 0x18) / 2)) % FRAMES;
        outb(nab + 0x15, (civ + 30) & 31);
    }
    u32 want = (pos + LAT) % FRAMES, n = (want + FRAMES - wp) % FRAMES;
    if (n > FRAMES / 2) {
        wp = pos;
        n = LAT;
    }
    while (n) {
        u32 k = FRAMES - wp;
        if (k > n)
            k = n;
        mix(wp, k);
        wp = (wp + k) % FRAMES;
        n -= k;
    }
    if (kind == S_USB)
        usb_audio_feed(D->ring, FRAMES, &D->upos);
}

int snd_ok(void) {
    return kind != 0;
}
