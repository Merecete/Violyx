#include "os.h"
/* ACPI power off: registers from the FADT, sleep values from the \_S5 object of the DSDT */
static void *ph(u64 a, u64 n) {
    mmio_map(a, n);
    return (void *)a;
}
static u8 *table(u64 a) {
    u8 *h = ph(a, 36);
    u32 l = *(u32 *)(h + 4);
    return ph(a, l);
}
/* Address of the extended PCI configuration space (MCFG), for registers from 0x100 up */
u64 acpi_mcfg(u64 rsdp) {
    if (!rsdp)
        return 0;
    u8 *r = ph(rsdp, 36);
    u64 sdt = r[15] >= 2 ? *(u64 *)(r + 24) : 0;
    int w = 8;
    if (!sdt) {
        sdt = *(u32 *)(r + 16);
        w = 4;
    }
    u8 *t = table(sdt);
    int n = (*(u32 *)(t + 4) - 36) / w;
    for (int i = 0; i < n; i++) {
        u64 e = w == 8 ? *(u64 *)(t + 36 + i * 8) : *(u32 *)(t + 36 + i * 4);
        u8 *h = table(e);
        if (memcmp(h, "MCFG", 4))
            continue;
        u32 l = *(u32 *)(h + 4);
        if (l >= 44 + 16 && *(u16 *)(h + 44 + 8) == 0 && h[44 + 10] == 0)
            return *(u64 *)(h + 44);
    }
    return 0;
}
void acpi_poweroff(u64 rsdp) {
    if (rsdp) {
        u8 *r = ph(rsdp, 36);
        u8 *fadt = 0;
        u64 sdt = r[15] >= 2 ? *(u64 *)(r + 24) : 0;
        int w = 8;
        if (!sdt) {
            sdt = *(u32 *)(r + 16);
            w = 4;
        }
        u8 *t = table(sdt);
        int n = (*(u32 *)(t + 4) - 36) / w;
        for (int i = 0; i < n && !fadt; i++) {
            u64 e = w == 8 ? *(u64 *)(t + 36 + i * 8) : *(u32 *)(t + 36 + i * 4);
            u8 *h = table(e);
            if (!memcmp(h, "FACP", 4))
                fadt = h;
        }
        if (fadt) {
            u32 flen = *(u32 *)(fadt + 4), smi = *(u32 *)(fadt + 48), a = *(u32 *)(fadt + 64),
                b = *(u32 *)(fadt + 68);
            u8 en = fadt[52];
            u64 ds = *(u32 *)(fadt + 40);
            if (!ds && flen >= 148)
                ds = *(u64 *)(fadt + 140);
            if (!a && flen >= 184)
                a = *(u32 *)(fadt + 176);
            if (ds && a) {
                u8 *d = table(ds);
                u32 l = *(u32 *)(d + 4);
                for (u32 i = 36; i + 16 < l; i++) {
                    if (memcmp(d + i, "_S5_", 4))
                        continue;
                    u8 *p = d + i + 4;
                    if (*p != 0x12)
                        continue;
                    p++;
                    p += (*p >> 6) + 2;
                    if (*p == 0x0A)
                        p++;
                    u16 ta = *p++;
                    if (*p == 0x0A)
                        p++;
                    u16 tb = *p;
                    if (smi && en && !(inw(a) & 1)) {
                        outb(smi, en);
                        for (int k = 0; k < 300000 && !(inw(a) & 1); k++)
                            udelay(1);
                    }
                    outw(a, (ta << 10) | (1 << 13));
                    if (b)
                        outw(b, (tb << 10) | (1 << 13));
                    udelay(200000);
                    break;
                }
            }
        }
    }
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400); /* Emulators, in case ACPI fails */
}
