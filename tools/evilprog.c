/* Test: tries forbidden things one after another. The mode comes from the file /home/mode */
#include "violyx.h"
extern char __bss_start;
int main(void) {
    char m[2] = {MODE, 0};
    uint8_t t = VX_ATTR(VC_BLACK, VC_GLOW);
    long r = 0;
    char b[64];
    switch (m[0]) {
        case 'a':
            *(volatile char *)0xFFFFFFFF80000000ull = 1;
            break; /* write to the kernel */
        case 'b':
            r = *(volatile long *)0xFFFF800000000000ull;
            break; /* read the HHDM */
        case 'c':
            __asm__("cli");
            break;
        case 'd':
            __asm__("hlt");
            break;
        case 'e':
            __asm__("outb %al,$0xE9");
            break;
        case 'f':
            for (;;)
                ;
        case 'g':
            ((void (*)(void))0)();
            break;
        case 'h':
            __asm__("ud2");
            break;
        case 'i': {
            volatile int z = 0;
            r = 1 / z;
            break;
        }
        case 'j':
            __asm__("mov $0x123,%rsp; push %rax");
            break;
        case 'k':
            __asm__("int $33");
            break;
        case 'l': {
            volatile char *c = (volatile char *)0x640000400000ull;
            c[0] = 1;
            break;
        } /* write to code */
        case 'm': {
            static char s[4] = {(char)0xC3};
            ((void (*)(void))s)();
            break;
        } /* execute data */
        case 'n': {
            int n = 0;
            while (vx_alloc(1 << 20))
                n++;
            vx_clear(t);
            vx_text(1, 1, "alloc stop", t);
            vx_number(12, 1, n, t);
            vx_show();
            break;
        }
        case 'o': {
            long a = vx_read("/system/config", b, sizeof b),
                 c = vx_read("/home/mode", (void *)0xFFFFFFFF80000000ull, 4),
                 d = vx_read("../../system/config", b, 8), e = vx_write("/programs/x", b, 1),
                 f = vx_read((char *)0xFFFFFFFF80000000ull, b, 8),
                 g = vx_sys(1, 0xFFFFFFFF80000000, 0, 0), h = vx_sys(99, 0, 0, 0),
                 i = vx_read("/home/mode", b, (size_t)-1), j = vx_list("/", b, 64);
            vx_clear(t);
            long v[9] = {a, c, d, e, f, g, h, i, j};
            for (int k = 0; k < 9; k++)
                vx_number(1, k, v[k], t);
            vx_show();
            for (;;)
                vx_wait(-1);
        }
        case 'p': {
            __asm__("mov $0,%eax; mov %eax,%ss");
            break;
        }
    }
    vx_clear(t);
    vx_text(1, 1, "survived", t);
    vx_number(10, 1, r, t);
    vx_show();
    for (;;)
        vx_wait(-1);
}
