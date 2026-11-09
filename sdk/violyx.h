/* Violyx SDK: everything a program needs. Programs run protected in their own address space. The
 * screen is a text grid: vx_put/vx_text write to it, vx_show displays it. */
#pragma once
#include <stddef.h>
#include <stdint.h>
enum {
    VK_MENU = 0x1C0, /* VK_MENU + n: item n of the menu set with vx_menu */
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
/* More pictures at once: slots 1-31 (0 is the one of vx_image), together up to 24 MB */
static inline long vx_image_slot(int slot, const uint8_t *px, int w, int h) {
    return vx_sys(24, (long)px, w, (long)h | (long)(slot & 31) << 16);
}
void vx_pixcell_slot(int x, int y, int slot, int ix, int iy);
/* Program menu: shown in the menu bar after Help, under the window title. Items are separated
   by |, for example vx_menu("Info|Music on/off"). Choosing item n sends the key VK_MENU + n. */
static inline long vx_menu(const char *items) {
    return vx_sys(25, (long)items, 0, 0);
}
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
/* Part of a file: up to n bytes (at most 512 KB per call) from offset off on. Returns the number
 * of bytes read, 0 at the end of the file. Large files read in parts keep everything else
 * running smoothly. */
static inline long vx_read_at(const char *p, void *b, unsigned long off, size_t n) {
    unsigned long q[3] = {(unsigned long)b, off, n};
    return vx_sys(26, (long)p, (long)q, 0);
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
/* Folder with sizes: each entry is 8 bytes size, 1 byte (1 = folder), the name and a 0. Returns
 * the number of bytes used. */
static inline long vx_list_sizes(const char *p, char *b, size_t n) {
    return vx_sys(27, (long)p, (long)b, (long)n);
}

/* Error codes (negative results) */
enum {
    VX_NOTFOUND = -1,
    VX_EXISTS = -2,
    VX_FULL = -3,
    VX_IOERR = -4,
    VX_INVALID = -7,
    VX_DENIED = -9,
    VX_FORMAT = -10,
    VX_BUSY = -12, /* the network is in use by another program */
    VX_AGAIN = -13 /* nothing there yet, try again later */
};

/* System dialogs. They look like all other Violyx dialogs and wait for the user. */
static inline long vx_dlg_(int kind, const char *t, const char *m, char *b, size_t n, int f) {
    unsigned long q[5] = {(unsigned long)t, (unsigned long)m, (unsigned long)b, n,
                          (unsigned long)f};
    return vx_sys(28, kind, (long)q, 0);
}
static inline void vx_message(const char *title, const char *text) {
    vx_dlg_(0, title, text, 0, 0, 0);
}
/* 1 = yes, 0 = no */
static inline int vx_ask(const char *title, const char *text) {
    return vx_dlg_(1, title, text, 0, 0, 0) > 0;
}
/* Input line, buf holds the suggestion. 1 = OK, 0 = cancelled */
static inline int vx_input(const char *title, const char *prompt, char *buf, size_t size) {
    return vx_dlg_(2, title, prompt, buf, size, 0) > 0;
}
/* The user chooses a file anywhere (also on sticks). The program may then read and write
 * exactly this file. 1 = chosen, path holds it */
static inline int vx_choose_file(const char *title, char *path, size_t size) {
    return vx_dlg_(3, title, 0, path, size, 0) > 0;
}
/* Opens a file in a system program: "write" (text), "view" (pictures), "web" (address) */
static inline long vx_open(const char *program, const char *file) {
    return vx_sys(30, 0, (long)program, (long)file);
}

/* Network: the kernel offers name lookups and TCP connections, everything above (HTTP, TLS) is up
 * to the program. The first use asks the user once whether the program may go online. One
 * connection at a time; while another program uses the network the calls return VX_BUSY.
 * Addresses are IPv4 in network order, as they come from vx_resolve. */
static inline int vx_net_ready(void) {
    char b[64];
    return vx_sys(29, 0, (long)b, sizeof b) == 1;
}
int vx_resolve(const char *name, uint32_t *ip); /* waits up to 6 s; 0 = found */
int vx_connect(uint32_t ip, int port);          /* waits up to 15 s; 0 = connected */
/* Sends: bytes taken, 0 = no room just now (wait a little), < 0 = connection lost */
static inline long vx_send(const void *b, size_t n) {
    return vx_sys(29, 5, (long)b, (long)n);
}
/* Receives: bytes, 0 = the other side has closed, VX_AGAIN = nothing yet */
static inline long vx_recv(void *b, size_t n) {
    return vx_sys(29, 6, (long)b, (long)n);
}
static inline void vx_disconnect(void) {
    vx_sys(29, 7, 0, 0);
}
#ifndef HOSTTEST
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
void *memmove(void *, const void *, size_t);
size_t strlen(const char *);
#endif
