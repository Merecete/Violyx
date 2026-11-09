#pragma once
/* Violyx system programs in ring 3. They were once part of the kernel; this header gives them the
 * same small set of functions they used there (windows, dialogs, files, network), now built on
 * the system calls of protected programs. A program sees only its own memory, every request goes
 * through the checks of the kernel. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "version.h"
#include "str.h"
#include "viofs.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
long vx_sys(long n, long a, long b, long c);
#define cfg_lang   ((int)vx_sys(10, 0, 0, 0))
#define TR(de, en) (cfg_lang ? (en) : (de))
#define ticks      ((u64)vx_sys(7, 0, 0, 0) / 10)

/* Memory */
void *kmalloc(size_t);
void kfree(void *);
void *k_calloc(size_t, size_t);
void panic(const char *);
static inline void udelay(u32 n) {
    while (n--)
        __asm__ volatile("pause");
}

/* Time */
typedef struct {
    u8 h, m, s, day, mon;
    u16 year;
} rtc_t;
void rtc_read(rtc_t *);
u32 rtc_unix(const rtc_t *);
void rtc_from_unix(u32, rtc_t *);
static inline u32 fs_now(void) {
    return (u32)vx_sys(6, 0, 0, 0);
}

/* Keys */
enum {
    K_MENU = 0x1C0,
    K_OPEN = 0x1BE, /* the program got a new file or address: sys_arg reads it */
    K_F1 = 0x100,
    K_F2,
    K_F3,
    K_F4,
    K_F5,
    K_F6,
    K_F7,
    K_F8,
    K_F9,
    K_F10,
    K_F11,
    K_F12,
    K_UP,
    K_DOWN,
    K_LEFT,
    K_RIGHT,
    K_HOME,
    K_END,
    K_PGUP,
    K_PGDN,
    K_INS,
    K_DEL,
    K_ENTER,
    K_ESC,
    K_BS,
    K_TAB
};
#define M_SHIFT 1
#define M_CTRL  2
#define M_ALT   4
typedef struct {
    u16 key;
    u8 mods;
} key_t;
int key_poll(key_t *);
key_t key_wait(void);
int ui_wait(key_t *);
static inline int key_down(u8 sc) {
    return (int)vx_sys(9, sc, 0, 0);
}

/* Colours */
enum {
    C_VOID,
    C_DESK,
    C_PANEL,
    C_RAISE,
    C_SEL,
    C_DIM,
    C_MID,
    C_SOFT,
    C_SHADOW,
    C_VIOLET,
    C_NEON,
    C_LAV,
    C_HOT,
    C_WALL,
    C_ORCH,
    C_GLOW
};
#define AT(b, f) (((b) << 4) | (f))

/* The window of the program */
struct win;
typedef struct {
    void (*draw)(struct win *);
    void (*key)(struct win *, key_t);
    void (*tick)(struct win *);
    int (*can_close)(struct win *);
    void (*destroy)(struct win *);
    void (*frame)(struct win *);
} app_t;
typedef struct win {
    int w, h; /* outer size like on the desktop: content plus frame */
    int zoomed;
    char title[40];
    const app_t *app;
    void *st;
    int cx, cy, cur;
    char foot[64];
    int fixed;
} win_t;
int ui_cols(void);
int ui_rows(void);
#define W     ui_cols()
#define H     ui_rows()
#define scr_w ui_cols()
#define scr_h ui_rows()
extern int ui_fixed, ui_quitting;
void put(int x, int y, u8 ch, u8 attr);
void put8(int x, int y, u8 ch, u8 fg, u8 bg);
void text(int x, int y, u8 attr, const char *s);
void fill(int x, int y, int w, int h, u8 ch, u8 attr);
void cput(win_t *, int x, int y, u8 ch, u8 attr);
void ctext(win_t *, int x, int y, u8 attr, const char *);
void cfill(win_t *, u8 attr);
void cput8(win_t *, int, int, u8, u8, u8);
int cw(win_t *);
int ch(win_t *);
/* Pictures: an id from gfx_newid, the pixels handed over with gfx_upload (w divisible by 4, h by
 * 8), then cputg shows the 4x8 block ix,iy in a cell */
u8 gfx_newid(void);
int gfx_upload(u8 id, const u8 *px, int w, int h);
void cputg(win_t *, int, int, u8, const u8 *, int, int, int);
u32 ui_rgb(int);
win_t *win_open(const char *t, int x, int y, int w, int h, const app_t *, void *st);
void win_close(win_t *);
void win_raise(win_t *);
void win_zoom(win_t *);
void win_title(win_t *, const char *);
void win_menu(win_t *, const char *name, const char *items);
void win_full(win_t *, int on);
int win_front(void);
void ui_render(void); /* shows what was drawn now, for progress during long work */
void ui_show(void);
void ui_background(int on);
void ui_screen(int *w, int *h); /* size of the whole screen in cells */
long prog_decode(int mode, const u8 *in, u64 n, u8 *out, u64 max, long a1, long a2, long a3, int ms,
                 int *flag);

/* Dialogs of the system */
void dlg_box(const char *, int w, int h, int *x, int *y);
void dlg_msg(const char *title, const char *text);
int dlg_confirm(const char *title, const char *text);
extern int dlg_fresh;
int dlg_input(const char *title, const char *prompt, char *buf, int max);
int dlg_pick(const char *title, char *out, int max);
int dlg_savechoice(void);

/* Files */
int fs_ok(void);
const char *fs_status(void);
const char *fsx_home(void);
u32 fsx_gen_now(void);
#define fsx_gen fsx_gen_now()
int fsx_stat(const char *, vfs_ent *);
int64_t fsx_read(const char *, void *, u64);
int64_t fsx_read_at(const char *, void *, u64 off, u64 max);
int fsx_write(const char *, const void *, u64, u32);
int fsx_mkdir(const char *, u32);
int fsx_remove(const char *);
int fsx_rename(const char *, const char *);
int fsx_list(const char *, int (*)(const vfs_ent *, void *), void *);
int fsx_rmtree(const char *);
int fsx_copy(const char *, const char *);
int fsx_quiet(const char *);
int fsx_first(char *, int);
void fsx_info(const char *, char *, int);
int fsx_eject(const char *);
int fsx_protected(const char *);
typedef struct {
    char name[251];
    int dir;
    u64 size;
} fs_item;
int fs_load(const char *path, fs_item **out);

/* System services (only for the built-in system programs) */
int sys_arg(char *, int);
int sys_report(int kind, char *, int);
enum {
    REP_USB,
    REP_SOUND,
    REP_NETDIAG,
    REP_DEVICES,
    REP_DEVICES_ALL,
    REP_MEM,
    REP_DF
};
int prog_open(const char *name, const char *arg);
static inline void app_write(const char *f) {
    prog_open("write", f);
}
static inline void app_view(const char *f) {
    prog_open("view", f);
}
static inline void app_clock(void) {
    prog_open("clock", 0);
}
static inline void web_app_open(const char *u) {
    prog_open("web", u);
}
int prog_install(const char *);
int prog_run(const char *);
int prog_run_file(const char *, const char *);
int prog_uninstall(const char *);
void drive_format(const char *);
int wall_open_file(const char *);
int wall_use(const u8 *);
void reboot(void);
static inline void snd_tone(int hz, int ms, int vol, int wave, int delay) {
    vx_sys(21, hz, ms, (long)(vol & 255) | (long)(wave & 15) << 8 | (long)(delay & 0xFFFF) << 16);
}

/* Network: the kernel keeps TCP and DNS, the program has everything above (HTTP, TLS) */
extern int net_aborted, net_bg;
int net_ready(void);
const char *net_status(void);
void net_info(char *, int, int);
int net_resolve(const char *, u32 *ip, char *txt);
int net_ping(u32 ip, u16 seq, int *ms, int *ttl);
int tcp_open(u32 ip, u16 port);
int tcp_tx(const void *, size_t);
int tcp_tx_all(const void *, int);
int tcp_rx(void *, size_t);
void tcp_end(void);

/* The program itself: opens its window, then the runtime takes over */
void prog_main(void);
void prog_reopen(void); /* optional: called after K_OPEN */
