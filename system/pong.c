/* Pong: a program in ring 3 */
#include "common.h"
/* ---------- Pong ---------- */
#define PH  4
#define WIN 7
typedef struct {
    int w, h, by, bx, vx, vy, py, cy, sp, sc, state, err;
    u32 rng;
} pong_t; /* Coordinates in 8.8 fixed point */
static int prnd(pong_t *p, int n) {
    p->rng = p->rng * 1103515245u + 12345u;
    return (p->rng >> 16) % n;
}
static void serve(pong_t *p, int dir) {
    p->bx = (p->w / 2) << 8;
    p->by = (1 + prnd(p, p->h - 2)) << 8;
    p->vx = dir * 0xB0;
    p->vy = (prnd(p, 2) ? 1 : -1) * (0x28 + prnd(p, 0x38));
    p->err = prnd(p, 5) - 2;
}
static void pong_reset(pong_t *p, win_t *w) {
    p->w = cw(w);
    p->h = ch(w);
    p->py = p->cy = ((p->h + 1 - PH) / 2) << 8;
    p->sp = p->sc = 0;
    p->state = 0;
    serve(p, 1);
}
static void bounce(pong_t *p, int pad, int dir) {
    int off = (p->by >> 8) - (pad >> 8) - PH / 2;
    p->vy = off * 0x38 + prnd(p, 0x18) - 0x0C;
    int s = (p->vx < 0 ? -p->vx : p->vx) + 0x0C;
    if (s > 0x1A0)
        s = 0x1A0;
    p->vx = dir * s;
}
static void pong_step(win_t *w) {
    pong_t *p = w->st;
    if (p->w != cw(w) || p->h != ch(w))
        pong_reset(p, w);
    if (p->state != 1)
        return;
    int maxp = (p->h - PH) << 8;
    if (key_down(0x48) || key_down(0x11))
        p->py -= 0xA0;
    if (key_down(0x50) || key_down(0x1F))
        p->py += 0xA0;
    if (p->py < 0x100)
        p->py = 0x100;
    if (p->py > maxp)
        p->py = maxp;
    int target = p->vx > 0 ? (p->by - ((PH / 2) << 8) + (p->err << 8)) : ((p->h - PH) / 2) << 8,
        spd = 0x48 + p->sp * 2, d = target - p->cy; /* with a reaction limit, so it can be beaten */
    p->cy += d > spd ? spd : d < -spd ? -spd : d;
    if (p->cy < 0x100)
        p->cy = 0x100;
    if (p->cy > maxp)
        p->cy = maxp;
    int ox = p->bx;
    p->bx += p->vx;
    p->by += p->vy;
    if (p->by < 0x100) {
        p->by = 0x200 - p->by;
        p->vy = -p->vy;
    }
    if (p->by >= (p->h) << 8) {
        p->by = (2 * p->h << 8) - p->by - 0x100;
        p->vy = -p->vy;
    }
    int lx = 3 << 8, rx = (p->w - 4) << 8, row = p->by >> 8;
    if (p->vx < 0 && ox >= lx && p->bx < lx && row >= p->py >> 8 && row < (p->py >> 8) + PH) {
        p->bx = lx;
        bounce(p, p->py, 1);
    } else if (p->vx > 0 && ox <= rx && p->bx > rx && row >= p->cy >> 8 &&
               row < (p->cy >> 8) + PH) {
        p->bx = rx;
        bounce(p, p->cy, -1);
    }
    if (p->bx < 0) {
        p->sc++;
        if (p->sc >= WIN)
            p->state = 2;
        else
            serve(p, 1);
    } else if (p->bx >= p->w << 8) {
        p->sp++;
        if (p->sp >= WIN)
            p->state = 2;
        else
            serve(p, -1);
    }
}
static void pong_draw(win_t *w) {
    pong_t *p = w->st;
    if (p->w != cw(w) || p->h != ch(w))
        pong_reset(p, w);
    u8 bg = AT(C_VOID, C_DIM);
    cfill(w, bg);
    char s[40];
    ksnprintf(s, 40, TR("Du  %d : %d  Violyx", "You  %d : %d  Violyx"), p->sp, p->sc);
    ctext(w, (p->w - (int)strlen(s)) / 2, 0, AT(C_VOID, C_GLOW), s);
    for (int y = 1; y < p->h; y += 2)
        cput(w, p->w / 2, y, 0xB3, bg);
    for (int i = 0; i < PH; i++) {
        cput(w, 2, (p->py >> 8) + i, 0xDB, AT(C_VOID, C_NEON));
        cput(w, p->w - 3, (p->cy >> 8) + i, 0xDB, AT(C_VOID, C_HOT));
    }
    if (p->state == 1 || p->state == 3)
        cput(w, p->bx >> 8, p->by >> 8, 0xFE, AT(C_VOID, C_GLOW));
    const char *m =
        p->state == 0   ? TR("Enter startet das Spiel", "Press Enter to start")
        : p->state == 3 ? TR("Pause - P geht weiter", "Paused - press P")
        : p->state == 2
            ? (p->sp >= WIN
                   ? TR("Gewonnen! Enter = Neues Spiel", "You win! Enter = New game")
                   : TR("Violyx gewinnt. Enter = Neues Spiel", "Violyx wins. Enter = New game"))
            : 0;
    if (m) {
        int l = 0;
        for (const char *q = m; *q; q++)
            l++;
        ctext(w, (p->w - l) / 2, p->h / 2, AT(C_VOID, C_LAV), m);
    }
    strlcpy_(w->foot, TR(" Pfeile oben/unten   P Pause ", " Up/Down arrows   P Pause "), 64);
}
static void pong_key(win_t *w, key_t k) {
    pong_t *p = w->st;
    if (k.key == K_ENTER) {
        if (p->state == 0)
            p->state = 1;
        else if (p->state == 2) {
            pong_reset(p, w);
            p->state = 1;
        }
    } else if (k.key == 'p' || k.key == 'P') {
        if (p->state == 1)
            p->state = 3;
        else if (p->state == 3)
            p->state = 1;
    }
}
static void pong_frame(win_t *w) {
    pong_t *p = w->st;
    int vx = p->vx, vy = p->vy, sc = p->sc, st = p->state;
    pong_step(w);
    if (st != 1)
        return;
    if (p->sc != sc || p->state != st) {
        snd_tone(180, 250, 55, 2, 0);
    } else if ((vx < 0) != (p->vx < 0))
        snd_tone(440, 50, 45, 0, 0);
    else if ((vy < 0) != (p->vy < 0))
        snd_tone(220, 35, 30, 0, 0);
}
static const app_t pong_app = {pong_draw, pong_key, 0, 0, simple_free, pong_frame};
void app_pong(void) {
    pong_t *p = kmalloc(sizeof *p);
    if (!p)
        return;
    memset(p, 0, sizeof *p);
    p->rng = (u32)ticks * 2654435761u + 1;
    win_t *w = win_open("Pong", 7, 2, 66, 20, &pong_app, p);
    if (w)
        w->fixed = 1;
    win_menu(w, "Pong", "*Info");
    if (!w) {
        kfree(p);
        return;
    }
    pong_reset(p, w);
}
void prog_main(void) {
    app_pong();
}
