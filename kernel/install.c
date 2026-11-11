#include "os.h"
/* Installer: writes Violyx to a whole disk, or next to another system into one partition or free
 * space. Every dialog defaults to No; nothing is written until an area is chosen, Yes is selected
 * and, when something gets erased, the word DELETE is typed. */
typedef struct {
    char name[8], pad[8];
} un;
static const u8 *img;
static u64 imglen;
void install_set_image(const void *p, u64 n) {
    img = p;
    imglen = n;
}
int install_available(void) {
    return img && imglen > 24 && !memcmp(img, "VIOINST1", 8);
}
static u32 crc32(const u8 *p, size_t n) {
    u32 c = ~0u;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}
static u64 need_sectors(void) {
    return *(u64 *)(img + 8);
}
static void describe(blkdev *d, char *o, int n, int *foreign) {
    static u8 s[512];
    *foreign = 1;
    if (d->rd(d, 0, s)) {
        strlcpy_(o, TR("nicht lesbar", "not readable"), n);
        return;
    }
    int zero = 1;
    for (int i = 0; i < 512; i++)
        if (s[i]) {
            zero = 0;
            break;
        }
    int mbr = s[510] == 0x55 && s[511] == 0xAA;
    int types = 0;
    for (int i = 0; i < 4; i++)
        if (s[446 + i * 16 + 4])
            types++;
    u8 h[512];
    int gpt = !d->rd(d, 1, h) && !memcmp(h, "EFI PART", 8);
    if (gpt) {
        u64 el = *(u64 *)(h + 72);
        u32 cnt = *(u32 *)(h + 80), esz = *(u32 *)(h + 84);
        int parts = 0, win = 0, vio = 0;
        if (esz >= 128 && esz <= 512 && cnt <= 256) {
            for (u32 i = 0; i < cnt; i++) {
                if (d->rd(d, el + i * esz / 512, s))
                    break;
                u8 *e = s + (i * esz) % 512;
                int used = 0;
                for (int k = 0; k < 16; k++)
                    if (e[k])
                        used = 1;
                if (!used)
                    continue;
                parts++;
                if (!memcmp(e, "VIOLYXFSPARTITN1", 16))
                    vio = 1;
                if (e[0] == 0xA2 && e[1] == 0xA0 && e[2] == 0xD0 && e[3] == 0xEB)
                    win = 1;
                if (e[0] == 0x16 && e[1] == 0xE3 && e[2] == 0xC9 && e[3] == 0xE3)
                    win = 1;
            }
        }
        ksnprintf(o, n, TR("%s %d Partition(en)%s", "%s %d partition(s)%s"),
                  vio ? "Violyx," : "GPT,", parts,
                  win ? TR(", WINDOWS MÖGLICH", ", MAY CONTAIN WINDOWS") : "");
        return;
    }
    if (mbr && types) {
        ksnprintf(o, n,
                  TR("MBR, %d Partition(en), enthält Daten", "MBR, %d partition(s), contains data"),
                  types);
        return;
    }
    if (zero && !mbr) {
        for (int k = 2; k < 64; k++) {
            if (d->rd(d, k * 2048, s))
                break;
            for (int i = 0; i < 512; i++)
                if (s[i]) {
                    strlcpy_(o, TR("keine Tabelle, aber Daten", "no table, but contains data"), n);
                    return;
                }
        }
        *foreign = 0;
        strlcpy_(o, TR("sieht leer aus", "looks empty"), n);
        return;
    }
    strlcpy_(o, TR("unbekannter Inhalt", "unknown content"), n);
}
static void sizestr(u64 sec, char *o, int n) {
    u64 mb = sec / 2048;
    if (mb >= 2048)
        ksnprintf(o, n, "%ld GB", mb / 1024);
    else
        ksnprintf(o, n, "%ld MB", mb);
}
static int choose_disk(void) {
    int n = blk_count(), s = 0;
    char t[100];
    int fo;
    for (;;) {
        int x, y;
        dlg_box(TR("Violyx installieren: Platte wählen", "Install Violyx: choose disk"), 70,
                n * 2 + 7, &x, &y);
        text(x + 3, y + 2, AT(C_RAISE, C_LAV),
             TR("Danach: gesamte Platte oder (bei UEFI) Partition/freier Bereich.",
                "Then: entire disk or (with UEFI) a partition or free space."));
        for (int i = 0; i < n; i++) {
            blkdev *d = blk_get(i);
            char z[16], c[60];
            sizestr(d->sectors, z, 16);
            describe(d, c, 60, &fo);
            int act = d == fs_device() && !fs_live();
            int bad = act || d->sectors < need_sectors();
            ksnprintf(t, 100, " %s %s: %s", d->name, z,
                      d->model[0] ? d->model : TR("unbekanntes Modell", "unknown model"));
            u8 a = i == s ? AT(C_SEL, C_GLOW) : bad ? AT(C_RAISE, C_DIM) : AT(C_RAISE, C_LAV);
            fill(x + 2, y + 4 + i * 2, 66, 2, ' ', a);
            text(x + 2, y + 4 + i * 2, a, t);
            ksnprintf(t, 100, "   %s",
                      act ? TR("Violyx läuft von dieser Platte, nicht wählbar",
                               "Violyx is running from this disk, not selectable")
                      : d->sectors < need_sectors() ? TR("zu klein", "too small")
                                                    : c);
            text(x + 2, y + 5 + i * 2, a, t);
        }
        text(x + 3, y + n * 2 + 5, AT(C_RAISE, C_LAV),
             TR("Pfeile wählen   Enter weiter   Esc abbrechen",
                "Arrows select   Enter continue   Esc cancel"));
        ui_show();
        key_t k = key_wait();
        if (k.key == K_ESC)
            return -1;
        if (k.key == K_UP && s)
            s--;
        else if (k.key == K_DOWN && s < n - 1)
            s++;
        else if (k.key == K_ENTER) {
            blkdev *d = blk_get(s);
            if ((d == fs_device() && !fs_live()) || d->sectors < need_sectors())
                continue;
            return s;
        }
    }
}
/* Warning: No is preselected, Enter cancels unless Yes was actively chosen */
static int warn(blkdev *d) {
    int yes = 0;
    char t[100], z[16], c[60];
    int fo;
    sizestr(d->sectors, z, 16);
    describe(d, c, 60, &fo);
    for (;;) {
        int x, y;
        dlg_box(TR("ACHTUNG", "WARNING"), 70, 15, &x, &y);
        u8 h = AT(C_HOT, C_VOID);
        fill(x + 2, y + 2, 66, 3, ' ', h);
        text(x + 4, y + 2, h,
             TR("ALLE DATEN AUF DIESER PLATTE WERDEN", "ALL DATA ON THIS DISK WILL BE"));
        text(x + 4, y + 3, h, TR("UNWIDERRUFLICH GELÖSCHT.", "PERMANENTLY ERASED."));
        text(x + 4, y + 4, h,
             TR("Sämtliche Partitionen gehen verloren!", "All partitions will be lost!"));
        ksnprintf(t, 100, TR("Platte:  %s, %s", "Disk:    %s, %s"), d->name, z);
        text(x + 3, y + 6, AT(C_RAISE, C_LAV), t);
        ksnprintf(t, 100, TR("Modell:  %s", "Model:   %s"),
                  d->model[0] ? d->model : TR("unbekannt", "unknown"));
        text(x + 3, y + 7, AT(C_RAISE, C_LAV), t);
        ksnprintf(t, 100, TR("Inhalt:  %s", "Content: %s"), c);
        text(x + 3, y + 8, AT(C_RAISE, C_LAV), t);
        text(x + 3, y + 10, AT(C_RAISE, C_GLOW), TR("Wirklich fortfahren?", "Really continue?"));
        text(x + 24, y + 12, yes ? AT(C_RAISE, C_LAV) : AT(C_SEL, C_GLOW),
             TR("  Nein  ", "  No  "));
        text(x + 36, y + 12, yes ? AT(C_HOT, C_VOID) : AT(C_RAISE, C_LAV), TR("  Ja  ", "  Yes  "));
        ui_show();
        key_t k = key_wait();
        if (k.key == K_ESC || k.key == 'n' || k.key == 'N')
            return 0;
        if (k.key == K_LEFT)
            yes = 0;
        else if (k.key == K_RIGHT)
            yes = 1;
        else if (k.key == K_ENTER)
            return yes;
    }
}
static void bar(int x, int y, u64 a, u64 b, const char *t) {
    dlg_box(TR("Violyx installieren", "Install Violyx"), 60, 8, &x, &y);
    text(x + 3, y + 2, AT(C_RAISE, C_LAV), t);
    int w = (int)(b ? a * 52 / b : 0);
    fill(x + 3, y + 4, 54, 1, 0xB0, AT(C_VOID, C_DIM));
    fill(x + 3, y + 4, w, 1, 0xDB, AT(C_VOID, C_NEON));
    text(x + 3, y + 6, AT(C_RAISE, C_LAV),
         TR("Bitte nicht ausschalten.", "Please do not turn off."));
    ui_show();
}
static int fixgpt(blkdev *d) {
    static u8 h[512], s[512];
    u64 tot = d->sectors;
    if (d->rd(d, 0, s))
        return -1;
    u64 v = tot - 1 > 0xFFFFFFFFu ? 0xFFFFFFFFu : tot - 1;
    for (int i = 0; i < 4; i++)
        if (s[446 + i * 16 + 4] == 0xEE)
            *(u32 *)(s + 446 + i * 16 + 12) = (u32)v;
    if (d->wr(d, 0, s))
        return -1;
    if (d->rd(d, 1, h) || memcmp(h, "EFI PART", 8))
        return -1;
    u64 el = *(u64 *)(h + 72);
    u32 cnt = *(u32 *)(h + 80), esz = *(u32 *)(h + 84);
    u32 secs = (cnt * esz + 511) / 512;
    if (secs > 128)
        return -1;
    u64 bk = tot - 1 - secs;
    u8 *tb = kmalloc(secs * 512);
    if (!tb)
        return -1;
    for (u32 i = 0; i < secs; i++)
        if (d->rd(d, el + i, tb + i * 512)) {
            kfree(tb);
            return -1;
        }
    for (u32 i = 0; i < cnt; i++) {
        u8 *e = tb + i * esz;
        if (!memcmp(e, "VIOLYXFSPARTITN1", 16))
            *(u64 *)(e + 40) = tot - secs - 3;
    }
    for (u32 i = 0; i < secs; i++)
        if (d->wr(d, el + i, tb + i * 512) || d->wr(d, bk + i, tb + i * 512)) {
            kfree(tb);
            return -1;
        }
    u32 ec = crc32(tb, cnt * esz);
    kfree(tb);
    *(u32 *)(h + 88) = ec;
    *(u64 *)(h + 32) = tot - 1;
    *(u64 *)(h + 48) = tot - secs - 2;
    *(u32 *)(h + 16) = 0;
    *(u32 *)(h + 16) = crc32(h, 92);
    if (d->wr(d, 1, h))
        return -1;
    *(u64 *)(h + 24) = tot - 1;
    *(u64 *)(h + 32) = 1;
    *(u64 *)(h + 72) = bk;
    *(u32 *)(h + 16) = 0;
    *(u32 *)(h + 16) = crc32(h, 92);
    if (d->wr(d, tot - 1, h))
        return -1;
    return d->fl(d);
}
static int write_all(blkdev *d) {
    u32 nrec = *(u32 *)(img + 16);
    const u8 *rec = img + 24;
    const u8 *data = rec + nrec * 16;
    u64 total = 0, done = 0;
    for (u32 i = 0; i < nrec; i++)
        total += *(u32 *)(rec + i * 16 + 8);
    static u8 z[512], s[512];
    memset(z, 0, 512);
    int x = 0, y = 0;
    const u8 *p = data;
    if ((u64)(data - img) > imglen)
        return -1;
    /* invalidate the old partition table at the end of the disk */ for (int i = 1; i <= 33; i++)
        if (d->wr(d, d->sectors - i, z))
            return -1;
    for (u32 i = 0; i < nrec; i++) {
        u64 l = *(u64 *)(rec + i * 16);
        u32 c = *(u32 *)(rec + i * 16 + 8), k = *(u32 *)(rec + i * 16 + 12);
        if (k) {
            if ((u64)(p + c * 512ull - img) > imglen)
                return -1;
        }
        for (u32 j = 0; j < c; j++) {
            if (k) {
                memcpy(s, p, 512);
                p += 512;
                if (d->wr(d, l + j, s))
                    return -1;
            } else if (d->wr(d, l + j, z))
                return -1;
            if (!(++done % 256))
                bar(x, y, done, total * 2, TR("Schreibe Violyx ...", "Writing Violyx ..."));
        }
    }
    d->fl(d);
    p = data;
    for (u32 i = 0; i < nrec; i++) {
        u64 l = *(u64 *)(rec + i * 16);
        u32 c = *(u32 *)(rec + i * 16 + 8), k = *(u32 *)(rec + i * 16 + 12);
        for (u32 j = 0; j < c; j++) {
            if (!k)
                continue; /* some zeroed ranges are overwritten by later data */
            if (d->rd(d, l + j, s))
                return -2;
            if (memcmp(s, p, 512))
                return -2;
            p += 512;
            if (!(++done % 256))
                bar(x, y, done, total * 2,
                    TR("Prüfe geschriebene Daten ...", "Verifying written data ..."));
        }
    }
    return fixgpt(d);
}
/* ---------- next to another system: into one partition or free space (GPT, UEFI) ----------
 * The chosen area gets a small boot partition of its own ("Violyx Boot", an EFI system partition
 * with Limine and the kernel) and behind it the Violyx data. The partitions around it and the EFI
 * system partition of the other system are not touched. A start entry "Violyx" in the firmware
 * makes it selectable; the other system stays the one that starts by itself. */
static const u8 T_ESP[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                             0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
static const u8 T_MSR[16] = {0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D,
                             0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE};
static const u8 T_DATA[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
                              0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
static const u8 T_WRE[16] = {0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D,
                             0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC};
static const u8 T_LNX[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
                             0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};
static const u8 T_SWAP[16] = {0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43,
                              0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F};
static const char T_VIO[16] = "VIOLYXFSPARTITN1";
#define ALIGN  2048ull
#define FS_MIN (128 * 1024ull) /* at least 64 MB for the data */
typedef struct {
    u8 h[512];
    u8 *tb;
    u32 cnt, esz, secs;
    u64 el, first, last, alt;
} gpt_t;
static u8 *ent(gpt_t *g, u32 i) {
    return g->tb + (u64)i * g->esz;
}
static int unused(const u8 *e) {
    for (int k = 0; k < 16; k++)
        if (e[k])
            return 0;
    return 1;
}
static int gpt_load(blkdev *d, gpt_t *g) {
    g->tb = 0;
    if (d->rd(d, 1, g->h) || memcmp(g->h, "EFI PART", 8))
        return -1;
    u32 hs = *(u32 *)(g->h + 12), c = *(u32 *)(g->h + 16);
    if (hs < 92 || hs > 512)
        return -1;
    *(u32 *)(g->h + 16) = 0;
    u32 cc = crc32(g->h, hs);
    *(u32 *)(g->h + 16) = c;
    g->el = *(u64 *)(g->h + 72);
    g->cnt = *(u32 *)(g->h + 80);
    g->esz = *(u32 *)(g->h + 84);
    g->first = *(u64 *)(g->h + 40);
    g->last = *(u64 *)(g->h + 48);
    g->alt = *(u64 *)(g->h + 32);
    if (c != cc || g->esz < 128 || g->esz > 512 || g->esz % 128 || !g->cnt || g->cnt > 1024 ||
        g->first > g->last || g->last >= d->sectors || g->alt >= d->sectors || g->el < 2 ||
        g->el >= d->sectors)
        return -1;
    g->secs = (g->cnt * g->esz + 511) / 512;
    /* the table at the start and the copy at the end lie outside the usable area */
    if (g->secs > 256 || g->el + g->secs > g->first || g->alt <= g->last ||
        g->alt - g->last <= g->secs)
        return -1;
    g->tb = kmalloc(g->secs * 512);
    if (!g->tb)
        return -1;
    for (u32 i = 0; i < g->secs; i++)
        if (d->rd(d, g->el + i, g->tb + i * 512)) {
            kfree(g->tb);
            g->tb = 0;
            return -1;
        }
    if (crc32(g->tb, g->cnt * g->esz) != *(u32 *)(g->h + 88)) {
        kfree(g->tb);
        g->tb = 0;
        return -1;
    }
    return 0;
}
typedef struct {
    u64 a, b;     /* the area, inclusive */
    int idx;      /* entry of the partition, -1 for free space */
    int boot;     /* "Violyx Boot" of an earlier installation in front of it, else -1 */
    int bad;      /* 1 protected, 2 too small */
    const u8 *ty; /* type of the partition */
    char name[37];
} slot_t;
#define NSLOT 40
static slot_t sl[NSLOT];
static int nsl;
static u64 esp_n; /* size of the boot partition: taken from the image */
static u64 up(u64 a) {
    return (a + ALIGN - 1) / ALIGN * ALIGN;
}
static void pname(const u8 *e, char *o) {
    int k = 0;
    for (int i = 0; i < 36; i++) {
        u16 c = *(const u16 *)(e + 56 + i * 2);
        if (!c)
            break;
        o[k++] = c >= 32 && c < 127 ? (char)c : '?';
    }
    o[k] = 0;
}
static const char *tname(const u8 *t) {
    if (!t)
        return TR("frei, nicht zugeordnet", "free, unallocated");
    if (!memcmp(t, T_ESP, 16))
        return TR("EFI-System", "EFI system");
    if (!memcmp(t, T_MSR, 16))
        return TR("Microsoft reserviert", "Microsoft reserved");
    if (!memcmp(t, T_DATA, 16))
        return TR("Basisdaten (Windows?)", "Basic data (Windows?)");
    if (!memcmp(t, T_WRE, 16))
        return TR("Windows-Wiederherstellung", "Windows recovery");
    if (!memcmp(t, T_LNX, 16))
        return "Linux";
    if (!memcmp(t, T_SWAP, 16))
        return "Linux-Swap";
    if (!memcmp(t, T_VIO, 16))
        return TR("Violyx (schon installiert)", "Violyx (installed before)");
    return TR("unbekannter Typ", "unknown type");
}
static void slots(gpt_t *g) {
    nsl = 0;
    int ord[128], n = 0;
    for (u32 i = 0; i < g->cnt && n < 128; i++) {
        u8 *e = ent(g, i);
        if (unused(e) || *(u64 *)(e + 40) < *(u64 *)(e + 32))
            continue;
        int k = n++;
        while (k && *(u64 *)(ent(g, ord[k - 1]) + 32) > *(u64 *)(e + 32)) {
            ord[k] = ord[k - 1];
            k--;
        }
        ord[k] = (int)i;
    }
    u64 pos = g->first;
    for (int j = 0; j <= n && nsl < NSLOT; j++) {
        u64 nx = j < n ? *(u64 *)(ent(g, ord[j]) + 32) : g->last + 1;
        if (nx > pos && up(pos) < nx && nx - up(pos) >= ALIGN) { /* free space in front */
            slot_t *s = &sl[nsl++];
            s->a = pos;
            s->b = nx - 1;
            s->idx = s->boot = -1;
            s->ty = 0;
            s->name[0] = 0;
            s->bad = s->b + 1 - up(s->a) < esp_n + FS_MIN ? 2 : 0;
            if (nsl == NSLOT)
                break;
        }
        if (j == n)
            break;
        u8 *e = ent(g, ord[j]);
        slot_t *s = &sl[nsl++];
        s->a = *(u64 *)(e + 32);
        s->b = *(u64 *)(e + 40);
        s->idx = ord[j];
        s->boot = -1;
        s->ty = e;
        pname(e, s->name);
        s->bad = 0;
        if (!memcmp(e, T_ESP, 16) || !memcmp(e, T_MSR, 16) || !memcmp(e, T_WRE, 16))
            s->bad = 1;
        /* outside the usable area or overlapping another one: a damaged table, not touched */
        if (s->a < g->first || s->b > g->last || s->a < pos)
            s->bad = 1;
        /* an earlier Violyx: its boot partition right in front belongs to it */
        if (!memcmp(e, T_VIO, 16) && j && nsl >= 2) {
            u8 *p = ent(g, ord[j - 1]);
            char pn[37];
            pname(p, pn);
            slot_t *q = &sl[nsl - 2];
            if (q->idx == ord[j - 1] && !memcmp(p, T_ESP, 16) && !strcmp(pn, "Violyx Boot")) {
                s->a = q->a;
                s->boot = q->idx;
                *q = *s; /* one line for both */
                s = q;
                nsl--;
            }
        }
        if (!s->bad && s->b + 1 - up(s->a) < esp_n + FS_MIN)
            s->bad = 2;
        if (s->b + 1 > pos)
            pos = s->b + 1;
    }
}
/* The disk image of the installer: sector l, or 0 when it is zero or missing */
static const u8 *isec(u64 l) {
    u32 nrec = *(u32 *)(img + 16);
    const u8 *rec = img + 24, *p = rec + nrec * 16;
    const u8 *r = 0;
    for (u32 i = 0; i < nrec; i++) {
        u64 a = *(u64 *)(rec + i * 16);
        u32 c = *(u32 *)(rec + i * 16 + 8), k = *(u32 *)(rec + i * 16 + 12);
        if (k && l >= a && l < a + c && (u64)(p + (l - a + 1) * 512 - img) <= imglen)
            r = p + (l - a) * 512;
        if (k)
            p += c * 512ull;
    }
    return r;
}
static u64 iesp_a, iesp_b, ifs_a, ifs_b;
static int image_parts(void) {
    const u8 *h = isec(1);
    if (!h || memcmp(h, "EFI PART", 8))
        return -1;
    u64 el = *(u64 *)(h + 72);
    u32 cnt = *(u32 *)(h + 80), esz = *(u32 *)(h + 84);
    if (esz < 128 || esz > 512 || cnt > 256)
        return -1;
    iesp_a = ifs_a = 0;
    for (u32 i = 0; i < cnt; i++) {
        const u8 *s = isec(el + i * esz / 512);
        if (!s)
            continue;
        const u8 *e = s + (i * esz) % 512;
        if (!memcmp(e, T_ESP, 16))
            iesp_a = *(u64 *)(e + 32), iesp_b = *(u64 *)(e + 40);
        if (!memcmp(e, T_VIO, 16))
            ifs_a = *(u64 *)(e + 32), ifs_b = *(u64 *)(e + 40);
    }
    if (!iesp_a || !ifs_a || iesp_b < iesp_a || ifs_b < ifs_a)
        return -1;
    esp_n = iesp_b - iesp_a + 1;
    return 0;
}
static void szs(u64 sec, char *o, int n) {
    sizestr(sec, o, n);
}
static int choose_slot(blkdev *d) {
    int s = 0;
    while (s < nsl - 1 && sl[s].bad)
        s++;
    char t[100], z[16];
    for (;;) {
        int x, y;
        dlg_box(TR("Partition oder freien Bereich wählen", "Choose partition or free space"), 74,
                nsl * 2 + 8, &x, &y);
        ksnprintf(t, 100, TR("%s: Violyx kommt nur in den gewählten Bereich.",
                             "%s: Violyx goes only into the chosen area."),
                  d->name);
        text(x + 3, y + 2, AT(C_RAISE, C_LAV), t);
        for (int i = 0; i < nsl; i++) {
            slot_t *q = &sl[i];
            szs(q->b + 1 - q->a, z, 16);
            char pn[12];
            if (q->idx < 0)
                strlcpy_(pn, TR("frei", "free"), 12);
            else if (q->boot >= 0)
                strlcpy_(pn, "Violyx", 12);
            else
                ksnprintf(pn, 12, "%s%d", TR("Teil ", "Part "), q->idx + 1);
            ksnprintf(t, 100, " %8s %8s %s%s%s%s", pn, z, tname(q->ty), q->name[0] ? "  \"" : "",
                      q->name, q->name[0] ? "\"" : "");
            t[69] = 0;
            u8 a = i == s ? AT(C_SEL, C_GLOW) : q->bad ? AT(C_RAISE, C_DIM) : AT(C_RAISE, C_LAV);
            fill(x + 2, y + 4 + i * 2, 70, 2, ' ', a);
            text(x + 2, y + 4 + i * 2, a, t);
            text(x + 2, y + 5 + i * 2, a,
                 q->bad == 1   ? TR("   geschützt, nicht wählbar", "   protected, not selectable")
                 : q->bad == 2 ? TR("   zu klein (mindestens 128 MB)",
                                    "   too small (at least 128 MB)")
                 : q->idx < 0  ? TR("   wird belegt, nichts wird gelöscht",
                                    "   gets used, nothing is erased")
                               : TR("   wird GELÖSCHT", "   will be ERASED"));
        }
        text(x + 3, y + nsl * 2 + 5, AT(C_RAISE, C_LAV),
             TR("Pfeile wählen   Enter weiter   Esc abbrechen",
                "Arrows select   Enter continue   Esc cancel"));
        ui_show();
        key_t k = key_wait();
        if (k.key == K_ESC)
            return -1;
        if (k.key == K_UP && s)
            s--;
        else if (k.key == K_DOWN && s < nsl - 1)
            s++;
        else if (k.key == K_ENTER && !sl[s].bad)
            return s;
    }
}
static int choose_mode(blkdev *d) {
    int s = 1;
    for (;;) {
        int x, y;
        dlg_box(TR("Violyx installieren", "Install Violyx"), 64, 11, &x, &y);
        text(x + 3, y + 2, AT(C_RAISE, C_LAV), TR("Wohin soll Violyx?", "Where should Violyx go?"));
        const char *o[2] = {TR(" Gesamte Platte (alles darauf wird gelöscht)",
                               " Entire disk (everything on it is erased)"),
                            TR(" Neben anderen Systemen: Partition oder freier Bereich",
                               " Next to other systems: partition or free space")};
        for (int i = 0; i < 2; i++) {
            u8 a = i == s ? AT(C_SEL, C_GLOW) : AT(C_RAISE, C_LAV);
            fill(x + 2, y + 4 + i, 60, 1, ' ', a);
            text(x + 2, y + 4 + i, a, o[i]);
        }
        text(x + 3, y + 8, AT(C_RAISE, C_LAV),
             TR("Pfeile wählen   Enter weiter   Esc abbrechen",
                "Arrows select   Enter continue   Esc cancel"));
        ui_show();
        key_t k = key_wait();
        if (k.key == K_ESC)
            return -1;
        if (k.key == K_UP || k.key == K_DOWN)
            s = !s;
        else if (k.key == K_ENTER)
            return s;
    }
    (void)d;
}
/* Warning for the chosen area: No is preselected */
static int warn_slot(blkdev *d, slot_t *q) {
    int yes = 0;
    char t[100], z[16];
    for (;;) {
        int x, y;
        dlg_box(TR("ACHTUNG", "WARNING"), 72, 17, &x, &y);
        u8 h = AT(C_HOT, C_VOID);
        fill(x + 2, y + 2, 68, 3, ' ', h);
        if (q->idx >= 0) {
            text(x + 4, y + 2, h,
                 TR("DIESE PARTITION WIRD UNWIDERRUFLICH GELÖSCHT.",
                    "THIS PARTITION WILL BE PERMANENTLY ERASED."));
            text(x + 4, y + 3, h,
                 TR("Alle anderen Partitionen bleiben, wie sie sind.",
                    "All other partitions stay as they are."));
            if (!memcmp(q->ty, T_DATA, 16))
                text(x + 4, y + 4, h,
                     TR("Das kann ein Windows-Laufwerk sein (z. B. D:)!",
                        "This may be a Windows drive (e.g. D:)!"));
        } else {
            text(x + 4, y + 2, h,
                 TR("Der freie Bereich wird für Violyx belegt.", "The free space is used for Violyx."));
            text(x + 4, y + 3, h,
                 TR("Die Partitionstabelle der Platte wird geändert.",
                    "The partition table of the disk is changed."));
        }
        szs(d->sectors, z, 16);
        ksnprintf(t, 100, TR("Platte:    %s, %s, %s", "Disk:      %s, %s, %s"), d->name, z,
                  d->model[0] ? d->model : TR("unbekannt", "unknown"));
        text(x + 3, y + 6, AT(C_RAISE, C_LAV), t);
        szs(q->b + 1 - q->a, z, 16);
        ksnprintf(t, 100, TR("Bereich:   %s, %s %s", "Area:      %s, %s %s"), z, tname(q->ty),
                  q->name);
        text(x + 3, y + 7, AT(C_RAISE, C_LAV), t);
        text(x + 3, y + 9, AT(C_RAISE, C_LAV),
             TR("Violyx bekommt dort eine eigene kleine Startpartition und",
                "Violyx gets its own small boot partition there and a start"));
        text(x + 3, y + 10, AT(C_RAISE, C_LAV),
             TR("einen Starteintrag \"Violyx\". Mit BitLocker: Schlüssel bereithalten.",
                "entry \"Violyx\". With BitLocker: keep the recovery key at hand."));
        text(x + 3, y + 12, AT(C_RAISE, C_GLOW), TR("Wirklich fortfahren?", "Really continue?"));
        text(x + 24, y + 14, yes ? AT(C_RAISE, C_LAV) : AT(C_SEL, C_GLOW),
             TR("  Nein  ", "  No  "));
        text(x + 36, y + 14, yes ? AT(C_HOT, C_VOID) : AT(C_RAISE, C_LAV), TR("  Ja  ", "  Yes  "));
        ui_show();
        key_t k = key_wait();
        if (k.key == K_ESC || k.key == 'n' || k.key == 'N')
            return 0;
        if (k.key == K_LEFT)
            yes = 0;
        else if (k.key == K_RIGHT)
            yes = 1;
        else if (k.key == K_ENTER)
            return yes;
    }
}
static void newguid(u8 *g) {
    for (int i = 0; i < 16; i += 4) {
        u32 r = net_rand() ^ (u32)(ticks * 2654435761u);
        memcpy(g + i, &r, 4);
    }
    g[7] = (g[7] & 0x0F) | 0x40; /* version 4 */
    g[8] = (g[8] & 0x3F) | 0x80;
}
static void setent(u8 *e, u32 esz, const void *ty, const u8 *id, u64 a, u64 b, const char *nm) {
    memset(e, 0, esz);
    memcpy(e, ty, 16);
    memcpy(e + 16, id, 16);
    *(u64 *)(e + 32) = a;
    *(u64 *)(e + 40) = b;
    for (int i = 0; nm[i] && i < 36; i++)
        *(u16 *)(e + 56 + i * 2) = (u8)nm[i];
}
/* Copies the boot and the data part of the image to S and behind; 0 ok, -1 write, -2 verify */
static int write_area(blkdev *d, u64 S, u64 E) {
    u32 nrec = *(u32 *)(img + 16);
    const u8 *rec = img + 24, *data = rec + nrec * 16;
    static u8 z[512], s[512];
    memset(z, 0, 512);
    u64 total = 0, done = 0;
    for (u32 i = 0; i < nrec; i++)
        total += *(u32 *)(rec + i * 16 + 8);
    int x = 0, y = 0;
    for (int pass = 0; pass < 2; pass++) {
        const u8 *p = data;
        for (u32 i = 0; i < nrec; i++) {
            u64 l = *(u64 *)(rec + i * 16);
            u32 c = *(u32 *)(rec + i * 16 + 8), k = *(u32 *)(rec + i * 16 + 12);
            if (k && (u64)(p + c * 512ull - img) > imglen)
                return -1;
            for (u32 j = 0; j < c; j++, p += k ? 512 : 0) {
                u64 v = l + j, t;
                if (v >= iesp_a && v <= iesp_b)
                    t = S + (v - iesp_a);
                else if (v >= ifs_a && v <= ifs_b && S + esp_n + (v - ifs_a) <= E)
                    t = S + esp_n + (v - ifs_a);
                else
                    continue;
                if (!pass) {
                    if (d->wr(d, t, k ? p : z))
                        return -1;
                } else if (k) { /* zeroed ranges may be overwritten by later data */
                    if (d->rd(d, t, s) || memcmp(s, p, 512))
                        return -2;
                }
                if (!(++done % 256))
                    bar(x, y, done, total * 2,
                        pass ? TR("Prüfe geschriebene Daten ...", "Verifying written data ...")
                             : TR("Schreibe Violyx ...", "Writing Violyx ..."));
            }
        }
        if (!pass && d->fl(d))
            return -1;
    }
    /* the boot sector of the boot partition tells where it starts */
    for (int b = 0; b <= 6; b += 6)
        if (!d->rd(d, S + b, s) && s[510] == 0x55 && s[511] == 0xAA && S <= 0xFFFFFFFFu) {
            *(u32 *)(s + 28) = (u32)S;
            if (d->wr(d, S + b, s))
                return -1;
        }
    return 0;
}
static int write_gpt(blkdev *d, gpt_t *g) {
    *(u32 *)(g->h + 88) = crc32(g->tb, g->cnt * g->esz);
    u32 hs = *(u32 *)(g->h + 12);
    /* the copy at the end: where its header says, if it is sound */
    static u8 b[512];
    u64 bel = g->alt - g->secs;
    if (!d->rd(d, g->alt, b) && !memcmp(b, "EFI PART", 8) && *(u64 *)(b + 72) > g->last &&
        *(u64 *)(b + 72) + g->secs <= g->alt)
        bel = *(u64 *)(b + 72);
    if (bel <= g->last)
        return -1;
    for (u32 i = 0; i < g->secs; i++)
        if (d->wr(d, g->el + i, g->tb + i * 512))
            return -1;
    *(u32 *)(g->h + 16) = 0;
    *(u32 *)(g->h + 16) = crc32(g->h, hs);
    if (d->wr(d, 1, g->h))
        return -1;
    for (u32 i = 0; i < g->secs; i++)
        if (d->wr(d, bel + i, g->tb + i * 512))
            return -1;
    memcpy(b, g->h, 512);
    *(u64 *)(b + 24) = g->alt;
    *(u64 *)(b + 32) = 1;
    *(u64 *)(b + 72) = bel;
    *(u32 *)(b + 16) = 0;
    *(u32 *)(b + 16) = crc32(b, hs);
    if (d->wr(d, g->alt, b))
        return -1;
    return d->fl(d);
}
static void note(const char *t, const char *a, const char *b, const char *c) {
    const char *l[3] = {a, b, c};
    for (;;) {
        int x, y;
        dlg_box(t, 68, 9, &x, &y);
        for (int i = 0; i < 3; i++)
            if (l[i])
                text(x + 3, y + 2 + i, AT(C_RAISE, C_LAV), l[i]);
        text(x + 28, y + 6, AT(C_SEL, C_GLOW), " [ Enter ] ");
        ui_show();
        key_t k = key_wait();
        if (k.key == K_ENTER || k.key == K_ESC)
            return;
    }
}
static void install_area(blkdev *d) {
    gpt_t g;
    if (image_parts() || gpt_load(d, &g)) {
        dlg_msg(TR("Installieren", "Install"),
                TR("Die Partitionstabelle (GPT) dieser Platte ist nicht lesbar.",
                   "The partition table (GPT) of this disk cannot be read."));
        return;
    }
    slots(&g);
    int i = nsl ? choose_slot(d) : -1;
    if (i < 0) {
        kfree(g.tb);
        return;
    }
    slot_t q = sl[i];
    /* the entries: the boot partition in front, the data behind */
    int ie = q.boot >= 0 ? q.boot : q.idx, iff = q.boot >= 0 ? q.idx : -1;
    for (u32 k = 0; k < g.cnt && (ie < 0 || iff < 0); k++)
        if (unused(ent(&g, k)) && (int)k != ie) {
            if (ie < 0)
                ie = (int)k;
            else
                iff = (int)k;
        }
    if (ie < 0 || iff < 0) {
        kfree(g.tb);
        dlg_msg(TR("Installieren", "Install"),
                TR("In der Partitionstabelle ist kein Eintrag mehr frei.",
                   "There is no free entry left in the partition table."));
        return;
    }
    if (!warn_slot(d, &q)) {
        kfree(g.tb);
        dlg_msg(TR("Installieren", "Install"),
                TR("Abgebrochen. Es wurde nichts verändert.", "Cancelled. Nothing was changed."));
        return;
    }
    if (q.idx >= 0) {
        char w[16];
        w[0] = 0;
        if (!dlg_input(TR("Letzte Bestätigung", "Final confirmation"),
                       TR("Zum Löschen der Partition LÖSCHEN eintippen:",
                          "Type DELETE to erase the partition:"),
                       w, 16) ||
            strcmp(w, TR("L\x99SCHEN", "DELETE"))) {
            kfree(g.tb);
            dlg_msg(TR("Installieren", "Install"),
                    TR("Abgebrochen. Es wurde nichts verändert.",
                       "Cancelled. Nothing was changed."));
            return;
        }
    }
    if (d == fs_device())
        fs_detach();
    u64 S = up(q.a), E = q.b;
    u8 ge[16], gf[16];
    newguid(ge);
    newguid(gf);
    int r = write_area(d, S, E);
    if (!r) {
        setent(ent(&g, (u32)ie), g.esz, T_ESP, ge, S, S + esp_n - 1, "Violyx Boot");
        setent(ent(&g, (u32)iff), g.esz, T_VIO, gf, S + esp_n, E, "Violyx Daten");
        r = write_gpt(d, &g);
    }
    kfree(g.tb);
    if (r == -2) {
        note(TR("Installieren", "Install"),
             TR("Prüfung fehlgeschlagen! Der Bereich ist unvollständig",
                "Verification failed! The area is incompletely written,"),
             TR("beschrieben, die Partitionstabelle wurde nicht geändert.",
                "the partition table was not changed."),
             0);
        return;
    }
    if (r) {
        note(TR("Installieren", "Install"),
             TR("Schreibfehler! Die Installation ist unvollständig.",
                "Write error! The installation is incomplete."),
             0, 0);
        return;
    }
    if (!fs_device() && !fs_attach(d))
        cfg_save();
    if (efi_add_boot(ie + 1, S, esp_n, ge))
        note(TR("Installiert", "Installed"),
             TR("Violyx ist installiert, aber der Starteintrag konnte nicht",
                "Violyx is installed, but the start entry could not be"),
             TR("angelegt werden. Im UEFI-Setup einen Eintrag für",
                "created. In the UEFI setup, add an entry for"),
             TR("\\EFI\\BOOT\\BOOTX64.EFI auf \"Violyx Boot\" anlegen.",
                "\\EFI\\BOOT\\BOOTX64.EFI on \"Violyx Boot\"."));
    else
        note(TR("Installiert", "Installed"),
             TR("Das andere System startet weiter von selbst. Violyx wählst",
                "The other system still starts by itself. Choose Violyx"),
             TR("du beim Einschalten im Startmenü der Firmware (oft F11,",
                "when switching on in the boot menu of the firmware (often"),
             TR("F12 oder F8) unter \"Violyx\".", "F11, F12 or F8) as \"Violyx\"."));
    if (dlg_confirm(TR("Fertig", "Done"), TR("Medium entfernen, dann neu starten?",
                                             "Remove the medium, then restart?")))
        reboot();
}
static void install_run(void) {
    if (!install_available()) {
        dlg_msg(TR("Installieren", "Install"),
                TR("Bitte Violyx dafür von der ISO oder dem USB-Stick starten.",
                   "Please start Violyx from the ISO or USB stick for this."));
        return;
    }
    if (!blk_count()) {
        dlg_msg(TR("Installieren", "Install"),
                TR("Keine unterstützte Platte gefunden.", "No supported disk found."));
        return;
    }
    int i = choose_disk();
    if (i < 0)
        return;
    blkdev *d = blk_get(i);
    /* next to another system: only with a partition table (GPT) and when started by UEFI, which
     * takes the start entry */
    static u8 h[512];
    if (efi_available() && !d->rd(d, 1, h) && !memcmp(h, "EFI PART", 8)) {
        int m = choose_mode(d);
        if (m < 0)
            return;
        if (m == 1) {
            install_area(d);
            return;
        }
    }
    if (!warn(d)) {
        dlg_msg(TR("Installieren", "Install"),
                TR("Abgebrochen. Es wurde nichts verändert.", "Cancelled. Nothing was changed."));
        return;
    }
    char w[16];
    w[0] = 0;
    if (!dlg_input(TR("Letzte Bestätigung", "Final confirmation"),
                   TR("Zum Löschen LÖSCHEN eintippen:", "Type DELETE to erase:"), w, 16) ||
        strcmp(w, TR("L\x99SCHEN", "DELETE"))) {
        dlg_msg(TR("Installieren", "Install"),
                TR("Abgebrochen. Es wurde nichts verändert.", "Cancelled. Nothing was changed."));
        return;
    }
    if (d == fs_device())
        fs_detach();
    int r = write_all(d);
    if (r == -2)
        dlg_msg(TR("Installieren", "Install"),
                TR("Prüfung fehlgeschlagen! Die Platte ist unvollständig beschrieben.",
                   "Verification failed! The disk is incompletely written."));
    else if (r)
        dlg_msg(TR("Installieren", "Install"),
                TR("Schreibfehler! Die Platte ist unvollständig beschrieben.",
                   "Write error! The disk is incompletely written."));
    else {
        fs_attach(d);
        cfg_save();
    }
    if (!r &&
        dlg_confirm(TR("Fertig", "Done"), TR("Installiert. Medium entfernen, dann neu starten?",
                                             "Installed. Remove the medium, then restart?")))
        reboot();
}

/* The disk list does not change during the installer: the selection must never slip to another disk
 */
void app_install(void) {
    usb_hold = 1;
    install_run();
    usb_hold = 0;
    drives_reset();
}
