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
        case 'q': { /* system services that belong to the built-in system programs only */
            static char big[4096];
            unsigned long q2[2] = {(unsigned long)big, sizeof big};
            long v[9] = {vx_sys(30, 3, 0, 0),
                         vx_sys(30, 11, (long)"/system", 0),
                         vx_sys(30, 2, 0, (long)q2),
                         vx_sys(30, 15, (long)big, 0),
                         vx_sys(30, 4, (long)"/home/x.vpk", 0),
                         vx_sys(30, 0, (long)"files", 0),
                         vx_sys(30, 0, (long)"write", (long)"/system/config"),
                         vx_sys(30, 99, 0, 0),
                         vx_sys(30, 14, 1, (long)q2)};
            vx_clear(t);
            for (int k = 0; k < 9; k++)
                vx_number(1, k, v[k], t);
            vx_show();
            for (;;)
                vx_wait(-1);
        }
        case 'r': { /* bad pointers and sizes in the new calls */
            static uint8_t px[960 * 800];
            unsigned long q[5] = {0xFFFFFFFF80000000ull, 0, 0, 0, 0};
            unsigned long q3[5] = {0, 0, (unsigned long)b, 100000, 0};
            long img = 0;
            for (int k = 1; k < 32 && !img; k++)
                img = vx_sys(24, (long)px, 960, 800 | (long)k << 16);
            long v[9] = {vx_sys(28, 0, (long)q, 0),
                         vx_sys(28, 2, (long)q3, 0),
                         vx_sys(28, 0, 0xFFFFFFFF80000000ull, 0),
                         vx_sys(29, 6, (long)b, 8),
                         vx_sys(29, 5, 0xFFFFFFFF80000000ull, 8),
                         vx_sys(27, (long)"/system", (long)b, 64),
                         vx_sys(23, 12, 100000, 100000),
                         img,
                         vx_sys(23, 99, 0, 0)};
            vx_clear(t);
            for (int k = 0; k < 9; k++)
                vx_number(1, k, v[k], t);
            vx_show();
            for (;;)
                vx_wait(-1);
        }
        case 's': { /* holes closed in the review of 0.2.0 */
            vx_write("/home/s.txt", "x", 1);
            long v[9] = {vx_sys(30, 0, (long)"write", (long)"x://../../system/config"),
                         vx_sys(23, 18, 200, 0) == vx_sys(23, 18, 200 & 15, 0),
                         vx_sys(23, 12, 0xFFFFFFFFl, 5),
                         vx_sys(23, 4, 30, 0xFFFFFFFFl),
                         vx_sys(29, 9, 0, (long)b),
                         vx_sys(15, (long)"/home", 0, 0),
                         vx_sys(16, (long)"/downloads", (long)"/home/dl", 0),
                         vx_sys(9, 0x1C, 0, 0),
                         vx_sys(30, 0, (long)"view", (long)"/home/s.txt")};
            vx_clear(t);
            for (int k = 0; k < 9; k++)
                vx_number(1, k, v[k], t);
            vx_show();
            for (;;)
                vx_wait(-1);
        }
        case 't': /* the browser as a way out: needs the internet permission of the program */
            vx_sys(30, 0, (long)"web", (long)"http://10.0.2.2:8080/?secret");
            break;
        case 'u': /* refuses every close, in full screen, and is busy when asked */
        case 'v': /* the same, but it waits again */
            vx_sys(23, 3, 2, 0);
            vx_sys(23, 11, 1, 0);
            for (;;) {
                vx_clear(VX_ATTR(VC_VIOLET, VC_GLOW));
                vx_text(1, 1, "never closes", VX_ATTR(VC_VIOLET, VC_GLOW));
                vx_show();
                vx_wait(-1);
                while (m[0] == 'u' ? vx_key() >= 0 : vx_key() != 0)
                    ;
            }
    }
    vx_clear(t);
    vx_text(1, 1, "survived", t);
    vx_number(10, 1, r, t);
    vx_show();
    for (;;)
        vx_wait(-1);
}
