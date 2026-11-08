#include "os.h"
#include "limine.h"
#define REQ __attribute__((used, section(".limine_requests"))) static volatile
REQ LIMINE_BASE_REVISION(3);
REQ struct limine_framebuffer_request fbreq = {.id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0};
REQ struct limine_memmap_request mmreq = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};
REQ struct limine_hhdm_request hhreq = {.id = LIMINE_HHDM_REQUEST, .revision = 0};
REQ struct limine_rsdp_request rsreq = {.id = LIMINE_RSDP_REQUEST, .revision = 0};
REQ struct limine_kernel_file_request kfreq = {.id = LIMINE_KERNEL_FILE_REQUEST, .revision = 0};
REQ struct limine_module_request modreq = {.id = LIMINE_MODULE_REQUEST, .revision = 0};

typedef struct __attribute__((packed)) {
    u16 lo, sel;
    u8 ist, flags;
    u16 mid;
    u32 hi, z;
} idt_e;
static idt_e idt[49];
static struct __attribute__((packed)) {
    u16 lim;
    u64 base;
} idtr, gdtr;
static u64 gdt[7] = {
    0, 0x00AF9A000000FFFF, 0x00CF92000000FFFF, 0x00CFF2000000FFFF, 0x00AFFA000000FFFF, 0, 0};
static struct __attribute__((packed)) {
    u32 r0;
    u64 rsp0, rsp1, rsp2, r1, ist[7], r2;
    u16 r3, iomap;
} tss;
extern u64 isr_table[];
void gdt_load(void *);
volatile u64 ticks;
u64 mem_total_kb;
static u64 hhdm;
static int pit_ok;
static u64 tsc0, tsc_tick;
u64 phys(const void *p) {
    return (u64)p - hhdm;
}

static inline u64 rdtsc(void) {
    u32 a, d;
    __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
    return (u64)d << 32 | a;
}
static void halt(void) {
    __asm__("cli");
    for (;;)
        __asm__("hlt");
}
void panic(const char *s) {
    __asm__("cli");
    outb(0xE9, 'P');
    char l[100];
    ksnprintf(l, 100, TR("Violyx angehalten: %s", "Violyx stopped: %s"), s);
    ui_panic(l, 0);
    halt();
}
void isr_handler(regs *r) {
    int user = r->cs & 3;
    if (r->no == 48) {
        if (user)
            proc_syscall(r);
        return;
    }
    if (r->no < 32 && user)
        proc_fault(r);
    if (r->no < 32) {
        outb(0xE9, 'P');
        char d[100];
        ksnprintf(d, 100,
                  TR("Ausnahme %d  Fehler %lx  RIP %lx", "Exception %d  error %lx  RIP %lx"),
                  (int)r->no, r->err, r->rip);
        ui_panic(TR("Violyx angehalten: CPU-Ausnahme", "Violyx stopped: CPU exception"), d);
        halt();
    }
    if (r->no == 32) {
        ticks++;
        xhci_poll();
        snd_tick();
    } else if (r->no == 33)
        kbd_irq();
    if (r->no >= 40)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
    if (user)
        proc_tick(r);
}
/* Waits for the next event. Without a PIT (some new Intel systems) time runs on the TSC. */
void idle(void) {
    usb_hotplug();
    prog_bg();
    if (pit_ok) {
        __asm__("sti;hlt");
        net_poll();
        return;
    }
    __asm__("pause");
    ticks = (rdtsc() - tsc0) / tsc_tick;
    __asm__("cli");
    xhci_poll();
    __asm__("sti");
    net_poll();
}
static void cpu_init(void) {
    u64 ts = (u64)&tss;
    tss.iomap = sizeof tss;
    tss.rsp0 = proc_stack_top();
    gdt[5] = (sizeof tss - 1) | (ts & 0xFFFFFF) << 16 | 0x89ull << 40 | ((ts >> 24) & 255) << 56;
    gdt[6] = ts >> 32;
    gdtr.lim = sizeof gdt - 1;
    gdtr.base = (u64)gdt;
    gdt_load(&gdtr);
    __asm__ volatile("ltr %%ax" ::"a"(0x28));
    {
        u64 c;
        __asm__ volatile("mov %%cr0,%0" : "=r"(c));
        c = (c & ~(u64)((1 << 2) | (1 << 3))) | (1 << 1) | (1 << 5);
        __asm__ volatile("mov %0,%%cr0" ::"r"(c));
        __asm__ volatile("mov %%cr4,%0" : "=r"(c));
        c |= (1 << 9) | (1 << 10);
        u32 a_ = 7, b_, c_ = 0, d_;
        __asm__ volatile("cpuid" : "+a"(a_), "=b"(b_), "+c"(c_), "=d"(d_));
        if (b_ & (1 << 7))
            c |= 1 << 20;
        __asm__ volatile("mov %0,%%cr4" ::"r"(c));
        u32 lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));
        lo |= 1 << 11;
        __asm__ volatile("wrmsr" ::"a"(lo), "d"(hi), "c"(0xC0000080));
    }
    for (int i = 0; i < 49; i++) {
        u64 a = isr_table[i];
        idt[i] = (idt_e){(u16)a,         0x08,           0, (u8)(i == 48 ? 0xEE : 0x8E),
                         (u16)(a >> 16), (u32)(a >> 32), 0};
    }
    idtr.lim = sizeof idt - 1;
    idtr.base = (u64)idt;
    __asm__ volatile("lidt %0" ::"m"(idtr));
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 32);
    outb(0xA1, 40);
    outb(0x21, 4);
    outb(0xA1, 2);
    outb(0x21, 1);
    outb(0xA1, 1);
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
    u32 div = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, div & 255);
    outb(0x40, div >> 8);
}
static void mem_init(void) {
    struct limine_memmap_response *m = mmreq.response;
    u64 best = 0, len = 0;
    for (u64 i = 0; i < m->entry_count; i++) {
        struct limine_memmap_entry *e = m->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;
        mem_total_kb += e->length / 1024;
        if (e->length > len) {
            best = e->base;
            len = e->length;
        }
    }
    if (len < (1 << 22))
        panic(TR("Zu wenig Speicher", "Not enough memory"));
    heap_init(best + hhdm, best + hhdm + len);
}
/* Device registers (MMIO) are mapped 1:1 and uncached in the lower half. This happens per device,
 * because depending on the firmware they can sit anywhere. */
static u64 *pml4;
u64 *kpml4(void) {
    return pml4;
}
u64 hhdm_off(void) {
    return hhdm;
}
static u64 *nexttbl(u64 *t, int i) {
    if (!(t[i] & 1)) {
        void *n = dma_alloc(4096);
        t[i] = phys(n) | 3;
    }
    return (u64 *)((t[i] & ~0xFFFull) + hhdm);
}
void mmio_map(u64 a, u64 n) {
    for (u64 p = a & ~0x1FFFFFull; p < a + n; p += 0x200000) {
        u64 *pdpt = nexttbl(pml4, (p >> 39) & 255), *pd = nexttbl(pdpt, (p >> 30) & 511);
        pd[(p >> 21) & 511] = p | 3 | (1 << 3) | (1 << 4) | (1 << 7);
    }
    u64 c;
    __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3" : "=r"(c));
}
static void paging_init(void) {
    u64 cr3;
    __asm__ volatile("mov %%cr3,%0" : "=r"(cr3));
    pml4 = (u64 *)((cr3 & ~0xFFFull) + hhdm);
    for (int i = 0; i < 256; i++)
        pml4[i] = 0;
    __asm__ volatile("mov %0,%%cr3" ::"r"(cr3));
}
static u8 cmos(u8 r) {
    outb(0x70, r);
    return inb(0x71);
}
static u8 bcd(u8 v) {
    return (v & 15) + (v >> 4) * 10;
}
static void pit_check(void) {
    __asm__("sti");
    u64 t = rdtsc();
    for (u64 i = 0; i < 400000000 && !ticks; i++) {
        __asm__("pause");
        if (rdtsc() - t > 4000000000ull)
            break;
    }
    if (ticks) {
        pit_ok = 1;
        return;
    }
    /* Fallback: calibrate the TSC on the second change of the CMOS clock */
    __asm__("cli");
    u8 s = cmos(0);
    while (cmos(0) == s)
        ;
    u64 a = rdtsc();
    s = cmos(0);
    while (cmos(0) == s)
        ;
    tsc_tick = (rdtsc() - a) / 100;
    tsc0 = rdtsc();
    __asm__("sti");
}
/* The hardware clock is never touched. Violyx stores the offset to the wanted time, so it also
 * survives reboots in QEMU. */
static int time_off;
int cfg_wall, cfg_lang = 1;
typedef struct {
    u32 magic;
    int off, wall, lang, kbd, vol, out, scheme, saver, swait;
} cfg_t;
#define CFG_NAME "/system/config"
static u32 to_secs(const rtc_t *t) {
    int y = t->year, m = t->mon;
    y -= m <= 2;
    int era = y / 400, yoe = y - era * 400,
        doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t->day - 1,
        doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (u32)(era * 146097 + doe - 719468) * 86400u + t->h * 3600u + t->m * 60u + t->s;
}
u32 rtc_unix(const rtc_t *t) {
    return to_secs(t);
}
void rtc_from_unix(u32 s, rtc_t *t);
static void from_secs(u32 s, rtc_t *t) {
    u32 d = s / 86400, r = s % 86400;
    t->h = r / 3600;
    t->m = (r / 60) % 60;
    t->s = r % 60;
    u32 z = d + 719468, era = z / 146097, doe = z - era * 146097,
        yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365,
        doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153,
        m = mp < 10 ? mp + 3 : mp - 9;
    t->day = doy - (153 * mp + 2) / 5 + 1;
    t->mon = m;
    t->year = yoe + era * 400 + (m <= 2);
}
void rtc_from_unix(u32 s, rtc_t *t) {
    from_secs(s, t);
}
static void rtc_raw(rtc_t *t) {
    for (int i = 0; i < 100000 && (cmos(10) & 0x80); i++)
        ;
    u8 fmt = cmos(11);
    u8 s = cmos(0), m = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9);
    u8 pm = h & 0x80;
    h &= 0x7F;
    if (!(fmt & 4)) {
        s = bcd(s);
        m = bcd(m);
        h = bcd(h);
        d = bcd(d);
        mo = bcd(mo);
        y = bcd(y);
    }
    if (!(fmt & 2)) {
        if (h == 12)
            h = 0;
        if (pm)
            h += 12;
    }
    t->h = h;
    t->m = m;
    t->s = s;
    t->day = d;
    t->mon = mo;
    t->year = 2000 + y;
}
void rtc_read(rtc_t *t) {
    rtc_t r;
    rtc_raw(&r);
    from_secs(to_secs(&r) + (u32)time_off, t);
}
void rtc_set(const rtc_t *t) {
    rtc_t r;
    rtc_raw(&r);
    time_off = (int)(to_secs(t) - to_secs(&r));
}
int cfg_save(void) {
    cfg_t c = {0x32464356,  time_off,    cfg_wall,       cfg_lang,      cfg_kbd,
               cfg_vol + 1, cfg_out + 1, cfg_scheme + 1, cfg_saver + 1, cfg_swait + 1};
    return fs_ok() ? vfs_write(CFG_NAME, &c, sizeof c, fs_now()) : -1;
}
void cfg_load(void) {
    cfg_t c;
    memset(&c, 0, sizeof c);
    int64_t n_ = fs_ok() ? vfs_read(CFG_NAME, &c, sizeof c) : -1;
    if (n_ >= 20 && c.magic == 0x32464356) {
        if (c.vol >= 1 && c.vol <= 11)
            cfg_vol = c.vol - 1;
        if (c.out >= 1 && c.out <= 3)
            cfg_out = c.out - 1;
        if (c.scheme >= 1 && c.scheme <= SCHEMES)
            cfg_scheme = c.scheme - 1;
        if (c.saver >= 1 && c.saver <= SAVERS)
            cfg_saver = c.saver - 1;
        if (c.swait >= 1 && c.swait <= SWAITS)
            cfg_swait = c.swait - 1;
        time_off = c.off;
        if (c.wall >= 0 && c.wall < WALLS)
            cfg_wall = c.wall;
        cfg_lang = c.lang == 1;
        cfg_kbd = c.kbd == 1;
    }
}
/* Orderly end before restart or power off: a message, the sound fades out, every drive empties
 * its cache and, before power off, parks or shuts down. */
static void sys_stop(int off) {
    ui_goodbye(off ? TR("Violyx wird beendet ...", "Violyx is shutting down ...")
                   : TR("Violyx wird neu gestartet ...", "Violyx is restarting ..."),
               off ? TR("Laufwerke werden sicher abgemeldet.", "Drives are being stopped safely.")
                   : TR("Daten werden sicher gespeichert.", "Saving data safely."));
    snd_fade();
    u64 t = ticks + 35;
    while (ticks < t)
        idle();
    blk_shutdown(off);
}
static void hw_reboot(void) {
    __asm__("cli");
    outb(0xE9, 'R');
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
        ;
    outb(0x64, 0xFE);
    udelay(50000);
    outb(0xCF9, 6);
    udelay(50000);
    struct __attribute__((packed)) {
        u16 l;
        u64 b;
    } z = {0, 0};
    __asm__("lidt %0; int3" ::"m"(z));
    halt();
}
void reboot(void) {
    sys_stop(0);
    hw_reboot();
}
void poweroff(void) {
    sys_stop(1);
    outb(0xE9, 'O');
    u64 r = rsreq.response ? (u64)rsreq.response->address : 0;
    if (r >= hhdm)
        r -= hhdm;
    acpi_poweroff(r);
    ui_goodbye(
        TR("Violyx ist beendet.", "Violyx has shut down."),
        TR("Du kannst den Rechner jetzt ausschalten.", "You can now turn off the computer."));
    halt();
}
/* Is d the drive Violyx was started from? Compares the disk ID (MBR) or the disk GUID (GPT)
 * that the boot loader reports. */
int boot_drive(blkdev *d) {
    struct limine_file *k = kfreq.response ? kfreq.response->kernel_file : 0;
    if (!k || !d || d->gone)
        return 0;
    static u8 s[512];
    if (k->mbr_disk_id && !d->rd(d, 0, s) && *(u32 *)(s + 440) == k->mbr_disk_id)
        return 1;
    static const u8 nul[16];
    if (memcmp(&k->gpt_disk_uuid, nul, 16) && !d->rd(d, 1, s) && !memcmp(s, "EFI PART", 8) &&
        !memcmp(s + 56, &k->gpt_disk_uuid, 16))
        return 1;
    /* The ISO written to a stick has neither: running without an installed system, a stick
     * with the Violyx ISO counts as the boot drive */
    if ((!fs_device() || fs_live()) && !d->rd(d, 64, s) && !memcmp(s + 1, "CD001", 5) &&
        !memcmp(s + 40, "VIOLYX", 6))
        return 1;
    return 0;
}
int fs_live(void) {
    return kfreq.response && kfreq.response->kernel_file &&
           kfreq.response->kernel_file->media_type == LIMINE_MEDIA_TYPE_OPTICAL;
}
void kmain(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED || !fbreq.response || !fbreq.response->framebuffer_count ||
        !mmreq.response || !hhreq.response)
        halt();
    hhdm = hhreq.response->offset;
    struct limine_framebuffer *f = fbreq.response->framebuffers[0];
    fb_init(f->address, f->width, f->height, f->pitch, f->bpp, f->red_mask_shift,
            f->green_mask_shift, f->blue_mask_shift);
    ui_init();
    cpu_init();
    mem_init();
    paging_init();
    kbd_init();
    pit_check();
    {
        u64 r = rsreq.response ? (u64)rsreq.response->address : 0;
        if (r >= hhdm)
            r -= hhdm;
        pci_ecam = acpi_mcfg(r);
    }
    static pci_t d[256];
    int n = pci_scan(d, 256); /* AMD boards have many functions, 64 was not always enough */
    for (int i = 0; i < n; i++)
        snd_probe(&d[i]);
    for (int i = 0; i < n; i++) {
        if (d[i].ven == 0x10EC && (d[i].did == 0x8126 || d[i].did == 0x8125))
            rge_probe(&d[i]);
        else if (d[i].cls == 1 && d[i].sub == 6 && d[i].pif == 1)
            ahci_init(&d[i]);
        else if (d[i].cls == 1 && d[i].sub == 8 && d[i].pif == 2)
            nvme_init(&d[i]);
        else if (d[i].cls == 0xC && d[i].sub == 3 && d[i].pif == 0x30)
            xhci_init(&d[i]);
        else if (d[i].cls == 2 && d[i].sub == 0 && d[i].ven == 0x8086 &&
                 (d[i].did == 0x100E || d[i].did == 0x100F || d[i].did == 0x1004 ||
                  d[i].did == 0x10D3))
            e1000_init(&d[i]);
    }
    ide_probe();
    if (modreq.response && modreq.response->module_count)
        install_set_image(modreq.response->modules[0]->address, modreq.response->modules[0]->size);
    fs_init();
    cfg_load();
    snd_select();
    drives_sync();
    ui_init();
    net_init();
    ui_splash();
    app_help();
    ui_run();
}
