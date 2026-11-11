#include "os.h"
#include "limine.h"
/* UEFI variables: the installer adds a start entry "Violyx" to the boot menu of the firmware, so a
 * Violyx installed next to another system can be chosen there (F11 and the like). The other
 * system stays first in the order. Only the runtime services of the firmware are used; Limine did
 * not move them (no SetVirtualAddressMap), so they run at their physical addresses, which are
 * mapped one to one in the lower half for this. */
#define REQ __attribute__((used, section(".limine_requests"))) static volatile
REQ struct limine_efi_system_table_request streq = {.id = LIMINE_EFI_SYSTEM_TABLE_REQUEST,
                                                    .revision = 0};
REQ struct limine_efi_memmap_request emreq = {.id = LIMINE_EFI_MEMMAP_REQUEST, .revision = 0};

#define EA __attribute__((ms_abi))
typedef u64 EA (*getvar_f)(const u16 *, const u8 *, u32 *, u64 *, void *);
typedef u64 EA (*setvar_f)(const u16 *, const u8 *, u32, u64, const void *);
typedef struct {
    u8 hdr[24];
    void *t[6]; /* GetTime ... ConvertPointer */
    getvar_f getvar;
    void *next;
    setvar_f setvar;
} efi_rt;
typedef struct {
    u8 hdr[24];
    u64 vendor;
    u32 rev, pad;
    u64 in_h, in, out_h, out, err_h, err;
    u64 rt, bs, n, cfg;
} efi_st;
typedef struct {
    u32 type, pad;
    u64 phys, virt, pages, attr;
} efi_md;
static const u8 GLOBAL[16] = {0x61, 0xDF, 0xE4, 0x8B, 0xCA, 0x93, 0xD2, 0x11,
                              0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C};
static efi_rt *rt;
static int state; /* 0 not tried, 1 ready, -1 none */

/* Maps a physical range one to one in 2 MB pages; what is mapped already stays as it is */
static void idmap(u64 a, u64 n, int uc) {
    u64 cr3;
    __asm__ volatile("mov %%cr3,%0" : "=r"(cr3));
    u64 *t4 = (u64 *)((cr3 & ~0xFFFull) + hhdm_off());
    for (u64 p = a & ~0x1FFFFFull; p < a + n; p += 0x200000) {
        u64 *t = t4;
        for (int l = 0; l < 2; l++) {
            int i = l ? (p >> 30) & 511 : (p >> 39) & 255;
            if (t[i] & (1 << 7)) /* a large page is there already: leave it */
                goto next;
            if (!(t[i] & 1)) {
                void *m = dma_alloc(4096);
                memset(m, 0, 4096);
                t[i] = phys(m) | 3;
            }
            t = (u64 *)((t[i] & ~0xFFFull) + hhdm_off());
        }
        u64 *e = &t[(p >> 21) & 511];
        if (!(*e & 1))
            *e = p | 3 | (1 << 7) | (uc ? (1 << 3) | (1 << 4) : 0);
    next:;
    }
    __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3" : "=r"(cr3));
}
static int efi_init(void) {
    if (state)
        return state > 0;
    state = -1;
    if (!streq.response || !emreq.response || !streq.response->address)
        return 0;
    const u8 *m = (const u8 *)emreq.response->memmap;
    u64 sz = emreq.response->memmap_size, ds = emreq.response->desc_size;
    if (!m || ds < sizeof(efi_md) || sz / ds > 4096)
        return 0;
    for (u64 o = 0; o + ds <= sz; o += ds) {
        const efi_md *d = (const efi_md *)(m + o);
        /* the runtime parts, and the boot services parts some firmware still touches */
        if ((d->attr >> 63) || d->type == 3 || d->type == 4)
            idmap(d->phys, d->pages * 4096, d->type == 11 || d->type == 12);
    }
    u64 a = (u64)streq.response->address;
    if (a >= hhdm_off())
        a -= hhdm_off();
    idmap(a, sizeof(efi_st), 0);
    efi_st *st = (efi_st *)a;
    if (memcmp(st->hdr, "IBI SYST", 8) || !st->rt)
        return 0;
    idmap(st->rt, sizeof(efi_rt), 0);
    rt = (efi_rt *)st->rt;
    if (memcmp(rt->hdr, "RUNTSERV", 8) || !rt->getvar || !rt->setvar)
        return 0;
    state = 1;
    return 1;
}
int efi_available(void) {
    return efi_init();
}
/* Firmware calls: interrupts off, the registers of the vector unit saved */
static u8 fx[512] __attribute__((aligned(16)));
static u64 get(const u16 *n, u32 *at, u64 *sz, void *b) {
    u64 f;
    __asm__ volatile("pushfq; pop %0; cli; fxsave64 %1" : "=r"(f), "=m"(fx));
    u64 r = rt->getvar(n, GLOBAL, at, sz, b);
    __asm__ volatile("fxrstor64 %0" ::"m"(fx));
    if (f & 0x200)
        __asm__ volatile("sti");
    return r;
}
static u64 set(const u16 *n, u32 at, u64 sz, const void *b) {
    u64 f;
    __asm__ volatile("pushfq; pop %0; cli; fxsave64 %1" : "=r"(f), "=m"(fx));
    u64 r = rt->setvar(n, GLOBAL, at, sz, b);
    __asm__ volatile("fxrstor64 %0" ::"m"(fx));
    if (f & 0x200)
        __asm__ volatile("sti");
    return r;
}
static void wname(u16 *o, const char *s) {
    while ((*o++ = (u8)*s++))
        ;
}
static void bootname(u16 *o, int k) {
    char t[9] = "Boot0000";
    for (int i = 0; i < 4; i++)
        t[7 - i] = "0123456789ABCDEF"[(k >> (i * 4)) & 15];
    wname(o, t);
}
/* Start entry for \EFI\BOOT\BOOTX64.EFI on the GPT partition idx (1-based) with the given place
 * and unique GUID. 0 = done, otherwise an error. */
int efi_add_boot(int idx, u64 start, u64 len, const u8 *guid) {
    if (!efi_init())
        return -1;
    static u8 b[512];
    static u16 nm[16];
    static const char *desc = "Violyx", *file = "\\EFI\\BOOT\\BOOTX64.EFI";
    u8 *p = b;
    *(u32 *)p = 1; /* LOAD_OPTION_ACTIVE */
    p += 6;        /* the length of the path comes later */
    for (const char *s = desc;; s++) {
        *(u16 *)p = (u8)*s;
        p += 2;
        if (!*s)
            break;
    }
    u8 *dp = p;
    p[0] = 4, p[1] = 1, *(u16 *)(p + 2) = 42; /* hard disk */
    *(u32 *)(p + 4) = (u32)idx;
    *(u64 *)(p + 8) = start;
    *(u64 *)(p + 16) = len;
    memcpy(p + 24, guid, 16);
    p[40] = 2, p[41] = 2; /* GPT, GUID signature */
    p += 42;
    int fl = (int)strlen(file);
    p[0] = 4, p[1] = 4, *(u16 *)(p + 2) = (u16)(4 + fl * 2 + 2); /* file */
    for (int i = 0; i <= fl; i++)
        *(u16 *)(p + 4 + i * 2) = (u8)file[i];
    p += 4 + fl * 2 + 2;
    p[0] = 0x7F, p[1] = 0xFF, *(u16 *)(p + 2) = 4; /* end */
    p += 4;
    *(u16 *)(b + 4) = (u16)(p - dp);
    u64 n = (u64)(p - b);
    /* an entry "Violyx" from before is used again, otherwise the first free number */
    int k = -1, fr = -1;
    static u8 o[1024];
    for (int i = 0; i < 0x100 && k < 0; i++) {
        bootname(nm, i);
        u32 at;
        u64 sz = sizeof o;
        u64 r = get(nm, &at, &sz, o);
        if (r == 0x800000000000000Eull) { /* not found */
            if (fr < 0)
                fr = i;
            continue;
        }
        if (r || sz < 6 + 14)
            continue;
        u16 *d = (u16 *)(o + 6);
        int same = 1;
        for (int j = 0; j < 7; j++)
            if (d[j] != (u16)(j < 6 ? desc[j] : 0))
                same = 0;
        if (same)
            k = i;
    }
    if (k < 0)
        k = fr;
    if (k < 0)
        return -2;
    bootname(nm, k);
    if (set(nm, 7, n, b))
        return -3;
    /* at the end of the order: the other system stays the one that starts by itself */
    static u16 ord[256];
    u32 at;
    u64 sz = sizeof ord;
    wname(nm, "BootOrder");
    u64 r = get(nm, &at, &sz, ord);
    if (r == 0x800000000000000Eull) /* there is no order yet */
        sz = 0;
    else if (r || sz % 2) /* unreadable or too long: never write an order that lost entries */
        return -4;
    for (u64 i = 0; i < sz / 2; i++)
        if (ord[i] == k)
            return 0;
    if (sz / 2 >= 256)
        return 0;
    ord[sz / 2] = (u16)k;
    return set(nm, 7, sz + 2, ord) ? -4 : 0;
}
