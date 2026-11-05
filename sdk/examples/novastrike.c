/* Novastrike - a vertically scrolling shooter for Violyx.
   SPDX-License-Identifier: MIT
   Arrows or WASD fly, Space fires, X raises the shield, C fires a nova that clears the
   screen, P pauses. Destroy a whole wave for a coin and a chance of a crystal.
   Crystals: W weapon, S shield, N nova. Each level ends with a boss. */
#include "violyx.h"

#define FW 60
#define FH 18
#define SHOTS 24
#define BULLETS 32
#define FOES 8
#define ITEMS 6
#define STARS 24
#define K_UP 0x48          /* scan codes for vx_held */
#define K_DOWN 0x50
#define K_LEFT 0x4B
#define K_RIGHT 0x4D
#define K_W 0x11
#define K_A 0x1E
#define K_S 0x1F
#define K_D 0x20
#define K_SPACE 0x39

typedef struct { int x, y, on; } dot;
typedef struct { int x, y, on, hp, born, kind; } foe;
typedef struct { int x, y, on; char kind; } item;

static dot shots[SHOTS], bullets[BULLETS], stars[STARS];
static foe foes[FOES];
static item items[ITEMS];
static int ship_x, ship_y, lives, weapon, shields, novas, shield_time, hurt_time;
static int score, coins, level, wave, wave_alive, wave_hits, wave_pause;
static int boss_on, boss_x, boss_y, boss_dir, boss_hp, boss_max, flash, cool;
static long frame, best;

/* one swing of a sine wave, scaled to +-12 */
static const signed char swing[16] = { 0, 5, 8, 11, 12, 11, 8, 5, 0, -5, -8, -11, -12, -11, -8, -5 };

static void add_bullet(int x, int y) {
    for (int i = 0; i < BULLETS; i++)
        if (!bullets[i].on) { bullets[i] = (dot){ x, y, 1 }; return; }
}

static void add_item(int x, int y, char kind) {
    for (int i = 0; i < ITEMS; i++)
        if (!items[i].on) { items[i] = (item){ x, y, 1, kind }; return; }
}

static void clear_shots(void) {
    for (int i = 0; i < BULLETS; i++) bullets[i].on = 0;
}

static void start_wave(void) {
    int home = 8 + vx_random(FW - 16);
    wave++;
    wave_alive = FOES;
    wave_hits = 0;
    for (int i = 0; i < FOES; i++)
        foes[i] = (foe){ home, -i * 2 - 1, 1, 1 + level / 3, (int)frame, wave % 3 };
}

static void start_boss(void) {
    boss_on = 1; boss_x = FW / 2 - 5; boss_y = 1; boss_dir = 1;
    boss_max = boss_hp = 30 + level * 15;
}

static void start_level(void) {
    wave = 0; wave_pause = 25; boss_on = 0;
    clear_shots();
}

static void new_game(void) {
    ship_x = FW / 2; ship_y = FH - 2;
    lives = 3; weapon = 1; shields = 2; novas = 2;
    score = coins = 0; level = 1;
    shield_time = hurt_time = 0;
    for (int i = 0; i < SHOTS; i++) shots[i].on = 0;
    for (int i = 0; i < FOES; i++) foes[i].on = 0;
    for (int i = 0; i < ITEMS; i++) items[i].on = 0;
    for (int i = 0; i < STARS; i++) stars[i] = (dot){ vx_random(FW), vx_random(FH), 1 + vx_random(2) };
    start_level();
}

static void fire(void) {
    static const int spread[3][3] = { { 0, 99, 99 }, { -1, 1, 99 }, { -1, 0, 1 } };
    if (cool > 0) return;
    cool = 4;
    for (int w = 0; w < 3; w++) {
        int off = spread[weapon - 1][w];
        if (off == 99) continue;
        for (int i = 0; i < SHOTS; i++)
            if (!shots[i].on) { shots[i] = (dot){ ship_x + off, ship_y - 1, 1 }; break; }
    }
}

static void kill_foe(foe *f) {
    f->on = 0;
    wave_alive--;
    wave_hits++;
    score += 20 * level;
    if (wave_hits == FOES) {               /* the whole wave was shot down */
        add_item(f->x, f->y > 0 ? f->y : 1, '$');
        if (vx_random(2) == 0) add_item(f->x + 2, f->y > 0 ? f->y : 1, "WSN"[vx_random(3)]);
    }
}

static void lose_life(void) {
    if (shield_time || hurt_time) return;
    lives--;
    hurt_time = 60;
    flash = 4;
    if (weapon > 1) weapon--;
    clear_shots();
}

static void use_nova(void) {
    if (!novas) return;
    novas--;
    flash = 6;
    clear_shots();
    for (int i = 0; i < FOES; i++)
        if (foes[i].on && foes[i].y >= 0) kill_foe(&foes[i]);
    if (boss_on) boss_hp -= 10;
}

static int near_ship(int x, int y) {
    return y == ship_y && x >= ship_x - 1 && x <= ship_x + 1;
}

static void hit_boss(void) {
    if (--boss_hp > 0) return;
    boss_on = 0;
    score += 500 * level + coins * 25;   /* coins turn into points at the end of a level */
    coins = 0;
    level++;
    add_item(boss_x + 5, boss_y + 2, "WSN"[vx_random(3)]);
    start_level();
}

static void update_boss(void) {
    if (frame % 2 == 0) {
        boss_x += boss_dir;
        if (boss_x <= 1 || boss_x >= FW - 12) boss_dir = -boss_dir;
    }
    int rate = 22 - level * 2;
    if (rate < 8) rate = 8;
    if (frame % rate == 0) {
        add_bullet(boss_x + 1, boss_y + 3);
        add_bullet(boss_x + 9, boss_y + 3);
        if (boss_hp * 2 < boss_max) add_bullet(boss_x + 5, boss_y + 3);   /* angry below half */
    }
}

static void update_wave(void) {
    if (!wave_alive) {
        if (--wave_pause > 0) return;
        wave_pause = 25;
        if (wave >= 5) start_boss(); else start_wave();
        return;
    }
    for (int i = 0; i < FOES; i++) {
        foe *f = &foes[i];
        if (!f->on) continue;
        if (frame % 3 == 0) f->y++;
        int t = (int)(frame - f->born) / 2 + i;
        int amount = f->kind == 0 ? 0 : f->kind == 1 ? swing[t & 15] : swing[t & 15] / 2;
        if (frame % 2 == 0) {
            f->x += amount / 6;
            if (f->x < 1) f->x = 1;
            if (f->x > FW - 2) f->x = FW - 2;
        }
        if (f->y >= 0 && vx_random(90 - (level > 8 ? 40 : level * 5)) == 0) add_bullet(f->x, f->y + 1);
        if (f->y >= 0 && near_ship(f->x, f->y)) { lose_life(); f->on = 0; wave_alive--; }
        if (f->y >= FH) { f->on = 0; wave_alive--; }
    }
}

static void update_shots(void) {
    for (int i = 0; i < SHOTS; i++) {
        dot *s = &shots[i];
        if (!s->on) continue;
        if (--s->y < 0) { s->on = 0; continue; }
        for (int f = 0; f < FOES; f++)
            if (foes[f].on && foes[f].y >= 0 && (s->y == foes[f].y || s->y + 1 == foes[f].y) && s->x == foes[f].x) {
                s->on = 0;
                if (--foes[f].hp <= 0) kill_foe(&foes[f]);
                break;
            }
        if (s->on && boss_on && s->x >= boss_x && s->x < boss_x + 11 && s->y >= boss_y && s->y < boss_y + 3) {
            s->on = 0;
            hit_boss();
        }
    }
    if (frame % 2 == 0)
        for (int i = 0; i < BULLETS; i++) {
            dot *b = &bullets[i];
            if (!b->on) continue;
            if (++b->y >= FH) b->on = 0;
            else if (near_ship(b->x, b->y)) { b->on = 0; lose_life(); }
        }
}

static void update_items(void) {
    if (frame % 4) return;
    for (int i = 0; i < ITEMS; i++) {
        item *it = &items[i];
        if (!it->on) continue;
        if (++it->y >= FH) { it->on = 0; continue; }
        if (near_ship(it->x, it->y) || near_ship(it->x, it->y + 1)) {
            it->on = 0;
            if (it->kind == '$') coins++;
            if (it->kind == 'W' && weapon < 3) weapon++;
            if (it->kind == 'S' && shields < 5) shields++;
            if (it->kind == 'N' && novas < 5) novas++;
        }
    }
}

static void update(void) {
    frame++;
    if (cool) cool--;
    if (shield_time) shield_time--;
    if (hurt_time) hurt_time--;
    if (flash) flash--;

    if ((vx_held(K_LEFT) || vx_held(K_A)) && ship_x > 1) ship_x--;
    if ((vx_held(K_RIGHT) || vx_held(K_D)) && ship_x < FW - 2) ship_x++;
    if (frame % 2 == 0) {                 /* cells are tall: vertical moves are slower */
        if ((vx_held(K_UP) || vx_held(K_W)) && ship_y > 8) ship_y--;
        if ((vx_held(K_DOWN) || vx_held(K_S)) && ship_y < FH - 1) ship_y++;
    }
    if (vx_held(K_SPACE)) fire();

    for (int i = 0; i < STARS; i++)
        if (frame % (3 - stars[i].on) == 0 && ++stars[i].y >= FH) { stars[i].y = 0; stars[i].x = vx_random(FW); }

    if (boss_on) update_boss(); else update_wave();
    update_shots();
    update_items();
}

static void draw(int state) {
    static const char *boss_art[3] = { " /=======\\ ", "<<|#####|>>", " \\=V===V=/ " };
    uint8_t text = VX_ATTR(VC_BLACK, VC_GLOW), dim = VX_ATTR(VC_BLACK, VC_MID);
    uint8_t ship = VX_ATTR(VC_BLACK, shield_time ? VC_GLOW : VC_NEON);
    vx_clear(flash ? VX_ATTR(VC_SEL, VC_GLOW) : dim);

    vx_text(0, 0, vx_english() ? "Score" : "Punkte", dim);    vx_number(7, 0, score, text);
    vx_text(16, 0, vx_english() ? "Best" : "Rekord", dim);    vx_number(23, 0, best, text);
    vx_text(32, 0, "L", dim);  vx_number(33, 0, lives, text);
    vx_text(36, 0, "$", dim);  vx_number(37, 0, coins, text);
    vx_text(41, 0, "X", dim);  vx_number(42, 0, shields, text);
    vx_text(45, 0, "C", dim);  vx_number(46, 0, novas, text);
    vx_text(49, 0, "Lv", dim); vx_number(51, 0, level, text);

    for (int i = 0; i < STARS; i++)
        vx_put(stars[i].x, stars[i].y + 1, stars[i].on == 2 ? '.' : (char)0xFA, VX_ATTR(VC_BLACK, stars[i].on == 2 ? VC_SOFT : VC_DIM));
    for (int i = 0; i < ITEMS; i++)
        if (items[i].on) vx_put(items[i].x, items[i].y + 1, items[i].kind, VX_ATTR(VC_RAISE, VC_GLOW));
    for (int i = 0; i < FOES; i++)
        if (foes[i].on && foes[i].y >= 0) vx_put(foes[i].x, foes[i].y + 1, foes[i].kind ? 'V' : 'W', VX_ATTR(VC_BLACK, foes[i].kind == 1 ? VC_ORCHID : VC_HOT));
    for (int i = 0; i < BULLETS; i++)
        if (bullets[i].on) vx_put(bullets[i].x, bullets[i].y + 1, '*', VX_ATTR(VC_BLACK, VC_HOT));
    for (int i = 0; i < SHOTS; i++)
        if (shots[i].on) vx_put(shots[i].x, shots[i].y + 1, (char)0xB3, text);
    if (boss_on) {
        for (int y = 0; y < 3; y++) vx_text(boss_x, boss_y + y + 1, boss_art[y], VX_ATTR(VC_BLACK, VC_HOT));
        for (int i = 0; i < 20; i++) vx_put(20 + i, 1, (char)(i * boss_max < boss_hp * 20 ? 0xDB : 0xB0), VX_ATTR(VC_BLACK, VC_ORCHID));
    }
    if (!hurt_time || (frame & 4)) {
        vx_text(ship_x - 1, ship_y + 1, "/A\\", ship);
        if (shield_time) { vx_put(ship_x - 2, ship_y + 1, '(', text); vx_put(ship_x + 2, ship_y + 1, ')', text); }
    }

    if (state == 1) vx_text(27, 8, "Pause", text);
    if (state == 2) {
        vx_text(25, 7, vx_english() ? "Game over" : "Spiel vorbei", text);
        vx_text(21, 9, vx_english() ? "Enter = play again" : "Enter = nochmal", dim);
    }
    vx_show();
}

int main(void) {
    int paused = 0;
    vx_read("best", &best, sizeof best);
    new_game();
    for (;;) {
        long next = vx_ms() + 40;
        int key;
        while (vx_ms() < next) {
            vx_wait(next - vx_ms());
            while ((key = vx_key()) != 0) {
                if (key == 'p' || key == 'P') paused = !paused;
                if ((key == 'x' || key == 'X') && shields && !shield_time) { shields--; shield_time = 75; }
                if ((key == 'c' || key == 'C') && lives > 0 && !paused) use_nova();
                if (key == VK_ENTER && lives <= 0) new_game();
            }
        }
        if (lives > 0 && !paused) update();
        if (score > best) { best = score; vx_write("best", &best, sizeof best); }
        draw(lives <= 0 ? 2 : paused ? 1 : 0);
    }
}
