/* Clock: an analog clock with a ticking second hand, or the seven-segment clock of the screen
 * saver, a program in ring 3. The face is drawn as a picture with smooth (anti-aliased) edges;
 * colours follow the colour scheme. A and D switch, the choice stays in /system/clock. */
#include "os.h"
#include "clockface.h"
#include "sevenseg.h"
#define ID0   1 /* picture slots 1 and 2, shown alternately */
#define CKFILE "/system/clock"
static int digital;
static u8 *dg;   /* the digital picture */
static int dgn, dgcur;
static void update_digital(win_t *w, clk_t *c, int pw, int ph) {
    if (c->t.s == c->sec && pw * ph == dgn)
        return;
    c->sec = c->t.s;
    if (pw * ph != dgn) {
        kfree(dg);
        dg = kmalloc((u64)pw * ph);
        dgn = dg ? pw * ph : 0;
        if (!dg)
            return;
    }
    memset(dg, C_PANEL, (u64)pw * ph);
    /* as big as fits, with a little room on every side */
    int dw = pw * 90 / 100 * 400 / ss_width(400), hdw = ph * 80 / 100 / 2;
    if (dw > hdw)
        dw = hdw;
    if (dw < 5)
        dw = 5;
    ssbuf b = {dg, pw, ph};
    ss_clock(&b, &c->t, (pw - ss_width(dw)) / 2, (ph - 2 * dw) / 2, dw, 0, C_NEON, C_RAISE, C_LAV);
    dgcur ^= 1;
    gfx_upload(ID0 + dgcur, dg, pw, ph);
    (void)w;
}
static void update(win_t *w) {
    clk_t *c = w->st;
    rtc_read(&c->t);
    int cols = cw(w), rows = ch(w) - 1;
    if (cols < 1 || rows < 1)
        return;
    if (digital) {
        update_digital(w, c, cols * 4, rows * 8);
        return;
    }
    if (c->t.s != c->sec || cols * 4 != c->pw || rows * 8 != c->ph) {
        c->sec = c->t.s;
        render(c, cols * 4, rows * 8);
        if (c->face) /* the kernel keeps a copy of the picture for the screen */
            gfx_upload(ID0 + c->cur, c->px[c->cur], c->pw, c->ph);
    }
}
static void ck_draw(win_t *w) {
    clk_t *c = w->st;
    win_title(w, TR("Uhr", "Clock"));
    update(w);
    cfill(w, AT(C_PANEL, C_ORCH));
    int cols = cw(w), rows = ch(w) - 1;
    if (digital && dg && dgn == cols * 4 * rows * 8)
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < cols; x++)
                cputg(w, x, y, ID0 + dgcur, dg, cols * 4, x, y);
    else if (!digital && c->face && c->pw == cols * 4 && c->ph == rows * 8)
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < cols; x++)
                cputg(w, x, y, ID0 + c->cur, c->px[c->cur], c->pw, x, y);
    static const char *wd[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"},
                      *we[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    rtc_t *t = &c->t;
    /* weekday after Sakamoto */
    static const u8 k[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int yy = t->year - (t->mon < 3);
    int d = (yy + yy / 4 - yy / 100 + yy / 400 + k[(t->mon + 11) % 12] + t->day) % 7;
    char b[40];
    if (cfg_lang)
        ksnprintf(b, 40, "%s %d-%02d-%02d  %02d:%02d:%02d", we[d], t->year, t->mon, t->day, t->h,
                  t->m, t->s);
    else
        ksnprintf(b, 40, "%s %02d.%02d.%d  %02d:%02d:%02d", wd[d], t->day, t->mon, t->year, t->h,
                  t->m, t->s);
    int l = strlen(b);
    ctext(w, (cols - l) / 2, rows, AT(C_PANEL, C_LAV), b);
}
static void ck_key(win_t *w, key_t k) {
    clk_t *c = w->st;
    int d = digital;
    if (k.key == 'a' || k.key == 'A' || k.key == K_MENU + 1)
        d = 0;
    else if (k.key == 'd' || k.key == 'D' || k.key == K_MENU + 2)
        d = 1;
    else if (k.key == ' ')
        d = !digital;
    if (d == digital)
        return;
    digital = d;
    c->sec = -1; /* draw the new face now */
    c->pw = c->ph = 0;
    dgn = 0;
    u8 v = (u8)digital;
    fsx_write(CKFILE, &v, 1, 0);
}
/* The window on top gets frames every 40 ms, so the second hand moves exactly with the RTC;
 * otherwise the once-a-second tick keeps it going. */
static void ck_frame(win_t *w) {
    update(w);
}
static void ck_tick(win_t *w) {
    update(w);
}
static void ck_free(win_t *w) {
    clk_t *c = w->st;
    freebufs(c);
    kfree(c);
    kfree(dg);
    dg = 0;
    dgn = 0;
}
static const app_t ck_app = {ck_draw, ck_key, ck_tick, 0, ck_free, ck_frame};
static void clock_open(void) {
    clk_t *c = kmalloc(sizeof *c);
    if (!c)
        return;
    memset(c, 0, sizeof *c);
    c->sec = -1;
    u8 v = 0;
    if (fsx_read(CKFILE, &v, 1) == 1)
        digital = v == 1;
    win_t *w = win_open(TR("Uhr", "Clock"), 24, 3, 32, 18, &ck_app, c);
    win_menu(w, TR("Uhr", "Clock"),
             TR("*Info|Analog (A)|Digital (D)", "*Info|Analog (A)|Digital (D)"));
}
void prog_main(void) {
    clock_open();
}
