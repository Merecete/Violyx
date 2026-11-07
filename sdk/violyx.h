/* Violyx SDK: everything a program needs. Programs run protected in their own address space. The
 * screen is a text grid: vx_put/vx_text write to it, vx_show displays it. */
#pragma once
#include <stddef.h>
#include <stdint.h>
enum {
    VK_F1 = 0x100,
    VK_F2,
    VK_F3,
    VK_F4,
    VK_F5,
    VK_F6,
    VK_F7,
    VK_F8,
    VK_F9,
    VK_F10,
    VK_F11,
    VK_F12,
    VK_UP,
    VK_DOWN,
    VK_LEFT,
    VK_RIGHT,
    VK_HOME,
    VK_END,
    VK_PGUP,
    VK_PGDN,
    VK_INS,
    VK_DEL,
    VK_ENTER,
    VK_ESC,
    VK_BS,
    VK_TAB
};
enum {
    VC_BLACK,
    VC_DESK,
    VC_PANEL,
    VC_RAISE,
    VC_SEL,
    VC_DIM,
    VC_MID,
    VC_SOFT,
    VC_SHADOW,
    VC_VIOLET,
    VC_NEON,
    VC_LAVENDER,
    VC_HOT,
    VC_WALL,
    VC_ORCHID,
    VC_GLOW
};
#define VX_ATTR(bg, fg) ((uint8_t)((bg) << 4 | (fg)))
typedef struct {
    uint64_t size, dir;
} vx_stat_t;
long vx_sys(long n, long a, long b, long c);
int main(void);
/* Screen. The window can change (F5 zooms): vx_cols/vx_rows always return the current size. */
int vx_cols(void);
int vx_rows(void);
void vx_clear(uint8_t attr);
void vx_put(int x, int y, char c, uint8_t attr);
/* Full colours: 0-15 Violyx, 16-231 colour cube, 232-255 grey. vx_rgb finds the closest colour for
 * 0-255 components. */
void vx_paint(int x, int y, char c, uint8_t fg, uint8_t bg);
uint8_t vx_rgb(int r, int g, int b);
void vx_text(int x, int y, const char *s, uint8_t attr);
void vx_number(int x, int y, long v, uint8_t attr);
void vx_show(void);
void vx_title(const char *s);
/* Keys: vx_wait waits for a key or the time in ms (-1 = as long as it takes). Esc always closes the
 * window. */
static inline void vx_wait(long ms) {
    vx_sys(2, ms, 0, 0);
}
static inline int vx_key(void) {
    long k = vx_sys(3, 0, 0, 0);
    return k < 0 ? 0 : (int)(k & 0xFFFF);
}
static inline int vx_held(int scancode) {
    return (int)vx_sys(9, scancode, 0, 0);
}
static inline void vx_exit(void) {
    vx_sys(0, 0, 0, 0);
    for (;;)
        ;
}
/* Time, memory, language */
static inline uint32_t vx_time(void) {
    return (uint32_t)vx_sys(6, 0, 0, 0);
}
static inline long vx_ms(void) {
    return vx_sys(7, 0, 0, 0);
}
/* Sound: frequency in Hz, length in ms, volume 1-100, delay in ms before the tone starts */
enum {
    VX_SQUARE,
    VX_TRIANGLE,
    VX_SAW,
    VX_NOISE
};
static inline void vx_tone(int hz, int ms, int vol, int wave, int delay) {
    vx_sys(21, hz, ms, (long)(vol & 255) | (long)(wave & 15) << 8 | (long)(delay & 0xFFFF) << 16);
}
/* Pictures: upload an image (w divisible by 4, h by 8, up to 960x800, one 256-colour index per
   pixel), then vx_pixcell(x,y,ix,iy) shows its 4x8-pixel block ix,iy in the text cell x,y. */
static inline long vx_image(const uint8_t *px, int w, int h) {
    return vx_sys(24, (long)px, w, h);
}
void vx_pixcell(int x, int y, int ix, int iy);
static inline int vx_english(void) {
    return (int)vx_sys(10, 0, 0, 0);
}
void *vx_alloc(size_t n); /* up to 256 MB, the memory is zeroed */
int vx_random(int n);
/* Files. Relative paths are in the program's own data folder. /home, /downloads and /drives are
 * allowed too. */
static inline long vx_read(const char *p, void *b, size_t n) {
    return vx_sys(11, (long)p, (long)b, (long)n);
}
static inline long vx_write(const char *p, const void *b, size_t n) {
    return vx_sys(12, (long)p, (long)b, (long)n);
}
static inline long vx_stat(const char *p, vx_stat_t *s) {
    return vx_sys(13, (long)p, (long)s, 0);
}
static inline long vx_mkdir(const char *p) {
    return vx_sys(14, (long)p, 0, 0);
}
static inline long vx_remove(const char *p) {
    return vx_sys(15, (long)p, 0, 0);
}
static inline long vx_rename(const char *a, const char *b) {
    return vx_sys(16, (long)a, (long)b, 0);
}
static inline long vx_list(const char *p, char *b, size_t n) {
    return vx_sys(17, (long)p, (long)b, (long)n);
}
#ifndef HOSTTEST
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
void *memmove(void *, const void *, size_t);
size_t strlen(const char *);
#endif
