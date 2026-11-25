#include "os.h"
#include "fat.h"
#include "version.h"
/* Updates: the system is one file, /boot/violyx.elf on the boot partition (FAT32). An update
 * (.vup) brings a new one, and on a stick also the new installer image /boot/install.bin. The
 * program Update checks the signature first; here the files are put into place.
 *
 * Safe against power failures: FAT replaces a file by writing it completely to new clusters and
 * then switching its directory entry in one sector. Order:
 *   1. the running system is copied to /boot/violyx-alt.elf (the previous version)
 *   2. limine.conf gets a boot menu with both versions (5 seconds, at the next start only)
 *   3. /boot/install.bin, if there is one
 *   4. /boot/violyx.elf
 * At every point there is a system that starts. When the new version has started, it puts the
 * normal limine.conf back. Whoever chooses the previous version in the menu is asked whether to go
 * back to it. */
static blkdev *ud;
static fatvol uv;
static int u_rd(uint64_t s, void *b) {
    return ud->rd(ud, s, b);
}
static int u_wr(uint64_t s, const void *b) {
    return ud->wr(ud, s, b);
}
static int u_rdn(uint64_t s, uint32_t n, void *b) {
    return ud->rdn(ud, s, n, b);
}
static int u_wrn(uint64_t s, uint32_t n, const void *b) {
    return ud->wrn(ud, s, n, b);
}
static int u_fl(void) {
    return ud->fl(ud);
}
/* The boot partition the running system came from: found by its GUID in the partition tables */
static int esp_mount(void) {
    const u8 *g = boot_part_guid();
    if (!g || fs_live())
        return VE_NOENT;
    static u8 h[512], s[512];
    for (int j = 0; j < blk_count_all(); j++) {
        blkdev *d = blk_get_all(j);
        if (d->cd || d->gone || d->rd(d, 1, h) || memcmp(h, "EFI PART", 8))
            continue;
        u64 el = *(u64 *)(h + 72);
        u32 cnt = *(u32 *)(h + 80), esz = *(u32 *)(h + 84);
        if (esz < 128 || esz > 512 || (esz & 127) || cnt > 256)
            continue;
        for (u32 i = 0; i < cnt; i++) {
            if (d->rd(d, el + i * esz / 512, s))
                break;
            u8 *e = s + (i * esz) % 512;
            if (memcmp(e + 16, g, 16))
                continue;
            u64 a = *(u64 *)(e + 32), b = *(u64 *)(e + 40);
            if (b < a || b >= d->sectors)
                return VE_FORMAT;
            ud = d;
            vfs_dev v = {u_rd, u_wr, u_fl, a, b - a + 1, d->rdn ? u_rdn : 0, d->wrn ? u_wrn : 0};
            return fat_mount(&uv, &v);
        }
    }
    return VE_NOENT;
}
static int has(const char *p) {
    vfs_ent e;
    return !fat_stat(&uv, p, &e);
}
/* limine.conf: normal (straight into Violyx) or with the menu of both versions */
static int write_conf(int menu, const char *newv, const char *oldv) {
    static char c[1200];
    int mod = has("/boot/install.bin"), k = 0;
    if (!menu)
        k = ksnprintf(c, sizeof c, "timeout: 0\nquiet: yes\n\n/Violyx\n    protocol: limine\n"
                                   "    path: boot():/boot/violyx.elf\n%s",
                      mod ? "    module_path: boot():/boot/install.bin\n" : "");
    else
        k = ksnprintf(c, sizeof c,
                      "# violyx-update: this menu is shown once after an update\n"
                      "timeout: 5\n\n/Violyx %s\n"
                      "    comment: %s\n    protocol: limine\n    path: boot():/boot/violyx.elf\n%s"
                      "/Violyx %s (%s)\n    protocol: limine\n"
                      "    path: boot():/boot/violyx-alt.elf\n%s",
                      newv, TR("Die neue Version", "The new version"),
                      mod ? "    module_path: boot():/boot/install.bin\n" : "", oldv,
                      TR("vorherige Version", "previous version"),
                      mod ? "    module_path: boot():/boot/install.bin\n" : "");
    return fat_write(&uv, "/limine.conf", c, (u64)k, fs_now());
}
static u8 *read_all(const char *p, u64 *n) {
    vfs_ent e;
    if (fat_stat(&uv, p, &e) || e.dir || !e.size || e.size > (64u << 20))
        return 0;
    u8 *b = kmalloc(e.size);
    if (!b)
        return 0;
    int64_t r = fat_read(&uv, p, b, e.size);
    if (r != (int64_t)e.size) {
        kfree(b);
        return 0;
    }
    *n = e.size;
    return b;
}
/* The version of a system file, from the .ver file next to it ("" when unknown) */
static void rdver(const char *p, char *o) {
    int64_t k = fat_read(&uv, p, o, 23);
    o[k > 0 ? k : 0] = 0;
    for (int i = 0; o[i]; i++)
        if (o[i] == '\n' || o[i] == '\r')
            o[i] = 0;
}
static int wrver(const char *p, const char *v) {
    char t[26];
    int k = ksnprintf(t, sizeof t, "%s\n", v);
    return fat_write(&uv, p, t, (u64)k, fs_now());
}
/* What is there: 0 = an update can be put in; alt receives the previous version ("" = none) */
int upd_info(char *alt, int n) {
    alt[0] = 0;
    int r = esp_mount();
    if (r)
        return r;
    if (!has("/boot/violyx.elf") || !has("/limine.conf"))
        return VE_FORMAT;
    if (has("/boot/violyx-alt.elf")) {
        char v[24];
        rdver("/boot/violyx-alt.ver", v);
        strlcpy_(alt, v[0] ? v : "?", n);
    }
    return 0;
}
/* The files of an update whose signature the program has checked. Header lines up to "end":
 * "version X", "file boot/violyx.elf SIZE", "file boot/install.bin SIZE"; then the files; then
 * 64 bytes of signature. */
int upd_apply(const u8 *b, u64 n) {
    if (n < 80 || memcmp(b, "VUP1\n", 5))
        return VE_FORMAT;
    u64 h = 0;
    for (u64 i = 5; i + 5 <= n && i < 65536; i++)
        if (!memcmp(b + i, "\nend\n", 5)) {
            h = i + 5;
            break;
        }
    if (!h)
        return VE_FORMAT;
    char ver[24] = "";
    struct {
        const char *name;
        u64 off, len;
    } f[2] = {{"/boot/violyx.elf", 0, 0}, {"/boot/install.bin", 0, 0}};
    u64 o = h;
    for (u64 i = 5; i < h;) {
        const char *l = (const char *)b + i;
        u64 e = i;
        while (e < h && b[e] != '\n')
            e++;
        int ln = (int)(e - i);
        if (ln > 8 && !memcmp(l, "version ", 8) && ln - 8 < (int)sizeof ver) {
            memcpy(ver, l + 8, ln - 8);
            ver[ln - 8] = 0;
        }
        for (int k = 0; k < 2; k++) {
            int nl = (int)strlen(f[k].name + 1); /* without the leading / */
            if (ln > 6 + nl && !memcmp(l, "file ", 5) && !memcmp(l + 5, f[k].name + 1, nl) &&
                l[5 + nl] == ' ') {
                u64 s = 0;
                for (int q = 6 + nl; q < ln && l[q] >= '0' && l[q] <= '9'; q++)
                    s = s * 10 + (u64)(l[q] - '0');
                f[k].off = o;
                f[k].len = s;
                o += s;
            }
        }
        if (ln == 5 && !memcmp(l, "notes", 5))
            break; /* the notes are text for people */
        i = e + 1;
    }
    if (!f[0].len || o + 64 != n || !ver[0])
        return VE_FORMAT;
    int r = esp_mount();
    if (r)
        return r;
    if (!has("/boot/violyx.elf"))
        return VE_FORMAT;
    /* 1. the running system becomes the previous version */
    u64 cn;
    u8 *cur = read_all("/boot/violyx.elf", &cn);
    if (!cur)
        return VE_IO;
    r = fat_write(&uv, "/boot/violyx-alt.elf", cur, cn, fs_now());
    kfree(cur);
    char cv[24];
    rdver("/boot/violyx.ver", cv);
    if (!r)
        r = wrver("/boot/violyx-alt.ver", cv[0] ? cv : VERSION);
    /* 2. the menu with both, 3. the installer image, 4. the new system */
    if (!r)
        r = write_conf(1, ver, VERSION);
    if (!r && f[1].len && has("/boot/install.bin"))
        r = fat_write(&uv, "/boot/install.bin", b + f[1].off, f[1].len, fs_now());
    if (!r)
        r = fat_write(&uv, "/boot/violyx.elf", b + f[0].off, f[0].len, fs_now());
    if (!r)
        r = wrver("/boot/violyx.ver", ver);
    if (!r && uv.d.fl())
        r = VE_IO;
    return r;
}
/* Back to the previous version: the two systems change places */
int upd_rollback(void) {
    char alt[24];
    int r = upd_info(alt, sizeof alt);
    if (r)
        return r;
    if (!alt[0])
        return VE_NOENT;
    u64 an, cn;
    u8 *a = read_all("/boot/violyx-alt.elf", &an), *c = read_all("/boot/violyx.elf", &cn);
    if (!a || !c) {
        kfree(a);
        kfree(c);
        return VE_IO;
    }
    char cv[24];
    rdver("/boot/violyx.ver", cv);
    if (!cv[0])
        strlcpy_(cv, "?", 24);
    /* the menu first: whichever file is where, both entries start something */
    r = write_conf(1, alt, cv);
    if (!r)
        r = fat_write(&uv, "/boot/violyx-alt.elf", c, cn, fs_now());
    if (!r)
        r = wrver("/boot/violyx-alt.ver", cv);
    if (!r)
        r = fat_write(&uv, "/boot/violyx.elf", a, an, fs_now());
    if (!r)
        r = wrver("/boot/violyx.ver", alt);
    if (!r)
        r = write_conf(0, 0, 0);
    kfree(a);
    kfree(c);
    if (!r && uv.d.fl())
        r = VE_IO;
    return r;
}
/* At the start, once the desktop is there: after an update the menu goes away again, or, when the
 * previous version was chosen, it asks whether to stay with it */
void upd_boot_check(void) {
    if (esp_mount())
        return;
    char c[64];
    int64_t k = fat_read(&uv, "/limine.conf", c, sizeof c - 1);
    if (k < 16)
        return;
    c[k] = 0;
    if (memcmp(c, "# violyx-update", 15))
        return;
    const char *p = boot_kernel_path();
    size_t pl = strlen(p);
    int alt = pl >= 14 && !strcmp(p + pl - 14, "violyx-alt.elf");
    const char *T_ = TR("Aktualisieren", "Update");
    /* the running system knows its version best: the label of its file is corrected */
    wrver(alt ? "/boot/violyx-alt.ver" : "/boot/violyx.ver", VERSION);
    if (!alt) {
        if (!write_conf(0, 0, 0)) {
            uv.d.fl();
            char t[120];
            ksnprintf(t, sizeof t, TR("Violyx ist jetzt Version %s.", "Violyx is now version %s."),
                      VERSION);
            dlg_msg(T_, t);
        }
        return;
    }
    char t[200];
    ksnprintf(t, sizeof t,
              TR("Du hast die vorherige Version %s gestartet. Bei ihr bleiben und die neue wieder "
                 "entfernen?",
                 "You started the previous version %s. Stay with it and remove the new one?"),
              VERSION);
    if (dlg_confirm(T_, t)) {
        int x, y;
        dlg_box(T_, 50, 5, &x, &y);
        text(x + 3, y + 2, AT(C_RAISE, C_LAV),
             TR("Bitte warten, nicht ausschalten ...", "Please wait, do not switch off ..."));
        ui_show();
        int r = upd_rollback();
        dlg_msg(T_, !r ? TR("Erledigt. Violyx startet wieder wie gewohnt mit dieser Version.",
                            "Done. Violyx starts as usual again, with this version.")
                       : TR("Das ging nicht. Das Startmenü bleibt, beide Versionen sind da.",
                            "That did not work. The boot menu stays, both versions are there."));
    }
}
