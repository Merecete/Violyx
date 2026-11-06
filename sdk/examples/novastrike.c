/* Novastrike - a vertically scrolling shooter for Violyx.
   SPDX-License-Identifier: MIT
   Arrows or WASD fly, Space fires, X raises the shield, C fires a nova that clears the
   screen, P pauses, M switches the music. Destroy a whole wave for a coin and a chance
   of a crystal. Crystals: W weapon, S shield, N nova.
   Ten worlds with their own colours, enemies, music and boss. Free all ten to win. */
#include "violyx.h"
#include "chip.h"

#define SHOTS 24
#define BULLETS 48
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

static int FW = 60, FH = 18;   /* field size, follows the window */

typedef struct { int x, y, on, dx, k; } dot;   /* k: 1 bounces off the walls, 2 snakes */
typedef struct { int x, y, on, hp, born, kind; } foe;
typedef struct { int x, y, on; char kind; } item;

static dot shots[SHOTS], bullets[BULLETS], stars[STARS];
static foe foes[FOES];
static item items[ITEMS];
static int ship_x, ship_y, lives, weapon, shields, novas, shield_time, hurt_time;
static int score, coins, level, wave, wave_alive, wave_hits, wave_pause, banner, warning;
static int boss_on, boss_x, boss_y, boss_dir, boss_hp, boss_max, boss_t, flash, cool;
static int beam_x, beam_t;     /* Lance: warning line, then a laser */
static int track_x, track_t;   /* Sentinel and Overlord: a wide laser that follows the ship */
static int spin, won;
static long frame, best;

static const chip_song song_nebula = {
  "a4 - c5 - e5 - c5 a4 f4 - a4 - c5 - a4 f4 e5 - g5 - e5 - c5 e5 d5 - b4 - g4 - - -",
  "a2 . a3 . a2 . a3 . f2 . f3 . f2 . f3 . c3 . c4 . c3 . c4 . g2 . g3 . g2 . g3 .",
  "k.h.s.h.k.h.s.hkk.h.s.h.k.h.s.hs", 120 };
static const chip_song song_ember = {
  "d5 d5 . f5 - e5 d5 c5 a4 - c5 d5 - . f4 g4 a4 - a4 c5 d5 - f5 a5 g5 - e5 - c5 - - -",
  "d2 d3 d2 d3 d2 d3 d2 d3 bb1 bb2 bb1 bb2 bb1 bb2 bb1 bb2 f2 f3 f2 f3 c2 c3 c2 c3 g2 g3 g2 g3 a2 a3 a2 a3",
  "k.skk.s.k.skk.ssk.skk.s.k.sks.ss", 105 };
static const chip_song song_toxic = {
  "e5 g5 b5 g5 e5 g5 b5 g5 f#5 a5 b5 a5 f#5 a5 b5 a5 c5 e5 g5 e5 c5 e5 g5 e5 d5 f#5 a5 f#5 d5 - - -",
  "e2 - e3 - e2 - e3 - b1 - b2 - b1 - b2 - c2 - c3 - c2 - c3 - d2 - d3 - d2 - d3 -",
  "k.h.s.hhk.h.s.hhk.h.s.hhk.h.s.ss", 110 };
static const chip_song song_frost = {
  "g5 - - eb5 - - c5 - bb4 - c5 - eb5 - g5 - ab5 - - f5 - - c5 - bb4 - - g4 - - - -",
  "c3 . g3 . c3 . g3 . c3 . g3 . bb2 . f3 . ab2 . eb3 . ab2 . eb3 . g2 . d3 . g2 . b2 .",
  "k...h...s...h...k...h...s...h.h.", 135 };
static const chip_song song_void = {
  "f#4 c#5 f#5 c#5 a5 g#5 f#5 c#5 d5 a4 d5 f#5 e5 d5 c#5 a4 b4 f#5 b5 f#5 a5 f#5 e5 c#5 f#5 - - - c#5 - - -",
  "f#2 f#2 . f#3 f#2 f#2 . f#3 d2 d2 . d3 d2 d2 . d3 b1 b1 . b2 b1 b1 . b2 c#2 c#2 . c#3 c#2 c#2 . c#3",
  "k.hhs.hhk.hhs.hhk.hhs.hhk.hhs.ss", 100 };
static const chip_song song_boss = {
  "e5 f5 e5 d#5 e5 f5 e5 b4 e5 f5 g5 f5 e5 d5 c5 b4 a4 bb4 a4 g#4 a4 bb4 a4 e4 a4 c5 e5 g5 f5 e5 d5 b4",
  "e2 e3 e2 e3 e2 e3 e2 e3 e2 e3 e2 e3 e2 e3 e2 e3 a1 a2 a1 a2 a1 a2 a1 a2 b1 b2 b1 b2 b1 b2 b1 b2",
  "kkhhskhhkkhhskhhkkhhskhhkkhhskss", 95 };
static const chip_song song_crystal = {
  "e5 b5 g#5 e5 c#6 b5 g#5 e5 f#5 c#6 a5 f#5 b5 a5 f#5 c#5 e5 b5 g#5 e5 c#6 b5 g#5 b5 a5 g#5 f#5 e5 d#5 - - -",
  "e3 . b3 . e3 . b3 . f#2 . c#3 . f#2 . c#3 . c#3 . g#3 . c#3 . g#3 . b2 . f#3 . b2 . d#3 .",
  "k.h.s.h.k.h.s.h.k.h.s.h.k.h.shs", 110 };
static const chip_song song_abyss = {
  "d4 - f4 - a4 - - g4 f4 - e4 - d4 - - - c4 - e4 - g4 - - f4 e4 - d4 - c#4 - - -",
  "d2 - - - d2 - a1 - bb1 - - - bb1 - f1 - c2 - - - c2 - g1 - a1 - - - a1 - e1 -",
  "k.......s.......k.......s...h.h.", 140 };
static const chip_song song_solar = {
  "c5 e5 g5 c6 b5 g5 e5 g5 a5 c6 e6 c6 b5 a5 g5 e5 f5 a5 c6 a5 g5 b5 d6 b5 c6 g5 e5 c5 d5 e5 f5 g5",
  "c3 c4 c3 c4 e3 e4 e3 e4 a2 a3 a2 a3 e3 e3 e3 e3 f2 f3 f2 f3 g2 g3 g2 g3 c3 c4 c3 c4 g2 g3 b2 b3",
  "k.hhs.hhk.hhs.hhk.hhs.hhkkhhs.ss", 95 };
static const chip_song song_steel = {
  "a4 a4 . a4 c5 . a4 . g4 g4 . g4 bb4 . g4 . f4 f4 . f4 a4 . f4 . e4 e4 . g#4 b4 . e5 .",
  "a1 a2 a1 a2 a1 a2 a1 a2 g1 g2 g1 g2 g1 g2 g1 g2 f1 f2 f1 f2 f1 f2 f1 f2 e1 e2 e1 e2 e1 e2 g#1 g#2",
  "k.k.s.k.k.k.s.kkk.k.s.k.k.k.s.ss", 100 };
static const chip_song song_eclipse = {
  "c5 - eb5 - g5 - c6 - b5 - g5 - d5 - b4 - ab4 - c5 - eb5 - ab5 - g5 - f5 - eb5 - d5 -",
  "c2 c3 c2 c3 c2 c3 c2 c3 g1 g2 g1 g2 g1 g2 g1 g2 ab1 ab2 ab1 ab2 ab1 ab2 ab1 ab2 g1 g2 g1 g2 b1 b2 b1 b2",
  "kkhhs.hhk.hhs.hhkkhhs.hhk.hhsss.", 105 };
static const chip_song song_victory = {
  "c5 e5 g5 c6 - - g5 c6 e6 - - - d6 - c6 - b5 - g5 - a5 b5 c6 - - - - - . . . .",
  "c3 . g3 . c3 . g3 . c3 . g3 . g2 . d3 . g2 . d3 . f2 . c3 . c3 . g2 . c3 . . .",
  "k.h.s.h.k.h.s.h.k.h.s.h.k.s.s...", 120 };

typedef struct {
    const char *name; const chip_song *song;
    uint8_t star, star2, hud, foe_a, foe_b, boss, bullet; char glyph_a, glyph_b;
    const char *boss_name; const char *art[5]; int aw, ah;
} world;
static const world worlds[10] = {
  { "Nebula", &song_nebula, 97, 54, 141, 201, 141, 201, 213, 'W', 'V', "Warden",
    { " /=======\\ ", "<<|#####|>>", " \\=V===V=/ " }, 11, 3 },
  { "Ember", &song_ember, 166, 88, 214, 208, 196, 202, 226, 'M', 'Y', "Inferno",
    { "  \\\\ || //  ", " /@@@@@@@@\\ ", "<#  (##)  #>", " \\vvvvvvvv/ ", "   \\/  \\/   " }, 12, 5 },
  { "Toxic", &song_toxic, 70, 22, 120, 82, 226, 118, 154, 'X', '%', "Hive",
    { " .-OOOOOOO-. ", "(oo:::::::oo)", " '-v-v-v-v-' " }, 13, 3 },
  { "Frost", &song_frost, 117, 24, 159, 51, 123, 195, 87, 'A', '+', "Lance",
    { "    /^^^\\    ", " --<|###|>-- ", "    \\_|_/    ", "      V      " }, 13, 4 },
  { "Void", &song_void, 244, 237, 250, 220, 255, 220, 231, '&', '$', "Oblivion",
    { "  .-=====-.  ", " / (O) (O) \\ ", "<|  \\___/  |>", " \\ vvvvvvv / ", "  '-=====-'  " }, 13, 5 },
  { "Crystal", &song_crystal, 159, 31, 195, 51, 213, 159, 219, '<', '>', "Prism",
    { "    /\\    ", "  /<##>\\  ", " <=<()>=> ", "  \\<##>/  ", "    \\/    " }, 10, 5 },
  { "Abyss", &song_abyss, 25, 17, 38, 37, 31, 44, 51, 'Q', 'U', "Kraken",
    { "  .-#####-.  ", " ( O     O ) ", "  )\\_-_-_//( ", " /|/|\\ /|\\|\\ " }, 13, 4 },
  { "Solar", &song_solar, 228, 136, 226, 214, 220, 208, 226, 'o', '@', "Helios",
    { "  \\  |  /  ", "-- (###) --", "  /  |  \\  " }, 11, 3 },
  { "Steel", &song_steel, 250, 240, 253, 196, 248, 160, 203, 'H', 'T', "Sentinel",
    { " [=]=====[=] ", "[|##|###|##|]", " \\===[_]===/ ", "     |X|     " }, 13, 4 },
  { "Eclipse", &song_eclipse, 167, 52, 220, 197, 178, 199, 220, 'Y', '*', "Overlord",
    { " _/\\_/^^^\\_/\\_ ", "<|  (@) (@)  |>", "<|   \\###/   |>", " \\=VVV=V=VVV=/ ", "    \\_/ \\_/    " }, 15, 5 },
};
static const world *W_;

/* one swing of a sine wave, scaled to +-12 */
static const signed char swing[16] = { 0, 5, 8, 11, 12, 11, 8, 5, 0, -5, -8, -11, -12, -11, -8, -5 };

static void add_bullet_k(int x, int y, int dx, int k) {
    for (int i = 0; i < BULLETS; i++)
        if (!bullets[i].on) { bullets[i] = (dot){ x, y, 1, dx, k }; return; }
}
static void add_bullet(int x, int y, int dx) { add_bullet_k(x, y, dx, 0); }

static void add_item(int x, int y, char kind) {
    for (int i = 0; i < ITEMS; i++)
        if (!items[i].on) { items[i] = (item){ x, y, 1, kind }; return; }
}

static void clear_shots(void) {
    for (int i = 0; i < BULLETS; i++) bullets[i].on = 0;
}

static void spawn_foe(int i, int x, int y, int kind) {
    foes[i] = (foe){ x, y, 1, 1 + level / 3, (int)frame, kind };
}

static void start_wave(void) {
    int home = 8 + vx_random(FW - 16);
    int kind = (wave + (level - 1) * 2) % 6;
    wave++;
    wave_alive = FOES;
    wave_hits = 0;
    for (int i = 0; i < FOES; i++) {
        int x = home, y = -i * 2 - 1;
        if (kind == 4) { x = FW / 2 + (i - 4) * 4; y = -(i < 4 ? 4 - i : i - 3) * 2; }   /* a V shape */
        if (kind == 5) { x = 4 + i * (FW - 8) / FOES; y = -1 - (i & 1) * 3; }             /* a line that dives */
        if (x < 1) x = 1;
        if (x > FW - 2) x = FW - 2;
        spawn_foe(i, x, y, kind);
    }
}

static void start_boss(void) {
    boss_on = 1; boss_x = FW / 2 - W_->aw / 2; boss_y = 1; boss_dir = 1; boss_t = 0; beam_t = 0; track_t = 0;
    boss_max = boss_hp = level == 10 ? 260 : 30 + level * 15;
    warning = 40;
    chip_play(&song_boss);
    for (int i = 0; i < 6; i++) vx_tone(i & 1 ? 440 : 660, 150, 45, VX_SAW, i * 160);
}

static void start_level(void) {
    W_ = &worlds[(level - 1) % 10];
    wave = 0; wave_pause = 25; boss_on = 0; banner = 55; warning = 0; beam_t = 0; track_t = 0;
    clear_shots();
    chip_play(W_->song);
    if (level > 1) for (int i = 0; i < 4; i++) vx_tone(392 + i * 131, 120, 45, VX_TRIANGLE, i * 110);
}

static void new_game(void) {
    ship_x = FW / 2; ship_y = FH - 2;
    lives = 3; weapon = 1; shields = 2; novas = 2; won = 0;
    score = coins = 0; level = 1;
    shield_time = hurt_time = 0;
    for (int i = 0; i < SHOTS; i++) shots[i].on = 0;
    for (int i = 0; i < FOES; i++) foes[i].on = 0;
    for (int i = 0; i < ITEMS; i++) items[i].on = 0;
    for (int i = 0; i < STARS; i++) stars[i] = (dot){ vx_random(FW), vx_random(FH), 1 + vx_random(2), 0 };
    start_level();
}

/* Follows the window size. The game keeps running when the window is zoomed with F5. */
static void fit_window(void) {
    FW = vx_cols();
    FH = vx_rows() - 1;
    if (ship_x > FW - 2) ship_x = FW - 2;
    if (ship_y > FH - 1) ship_y = FH - 1;
    if (boss_x > FW - W_->aw - 1) boss_x = FW - W_->aw - 1;
    if (boss_x < 1) boss_x = 1;
    for (int i = 0; i < FOES; i++) if (foes[i].x > FW - 2) foes[i].x = FW - 2;
}

static void fire(void) {
    static const int spread[3][3] = { { 0, 99, 99 }, { -1, 1, 99 }, { -1, 0, 1 } };
    if (cool > 0) return;
    cool = 2;
    vx_tone(1100, 40, 14, VX_SQUARE, 0);
    for (int w = 0; w < 3; w++) {
        int off = spread[weapon - 1][w];
        if (off == 99) continue;
        for (int i = 0; i < SHOTS; i++)
            if (!shots[i].on) { shots[i] = (dot){ ship_x + off, ship_y - 1, 1, 0 }; break; }
    }
}

static void kill_foe(foe *f) {
    f->on = 0;
    if (boss_on) { score += 10 * level; vx_tone(180, 90, 40, VX_NOISE, 0); return; }
    wave_alive--;
    wave_hits++;
    score += 20 * level;
    vx_tone(180, 90, 40, VX_NOISE, 0);
    if (wave_hits == FOES) {               /* the whole wave was shot down */
        add_item(f->x, f->y > 0 ? f->y : 1, '$');
        if (vx_random(2) == 0) add_item(f->x + 2, f->y > 0 ? f->y : 1, "WSN"[vx_random(3)]);
    }
}

static void lose_life(void) {
    if (shield_time || hurt_time) return;
    lives--;
    vx_tone(90, 500, 70, VX_NOISE, 0); vx_tone(70, 500, 45, VX_SAW, 0);
    hurt_time = 90;
    flash = 4;
    if (weapon > 1) weapon--;
    clear_shots();
}

static void beat_boss(void) {
    for (int i = 0; i < 6; i++) vx_tone(120 + i * 60, 160, 60, VX_NOISE, i * 90);
    boss_on = 0;
    for (int i = 0; i < FOES; i++) foes[i].on = 0;
    wave_alive = 0;
    score += 500 * level + coins * 25;   /* coins turn into points at the end of a level */
    coins = 0;
    if (level == 10) {                     /* the last world is free: the game is won */
        score += lives * 1000;
        won = 1; clear_shots(); chip_cur = 0; chip_play(&song_victory);
        return;
    }
    level++;
    add_item(boss_x + W_->aw / 2, boss_y + 2, "WSN"[vx_random(3)]);
    start_level();
}

static void use_nova(void) {
    if (!novas) return;
    novas--;
    for (int i = 0; i < 6; i++) vx_tone(1400 - i * 200, 90, 50, VX_SAW, i * 50);
    flash = 6;
    clear_shots();
    for (int i = 0; i < FOES; i++)
        if (foes[i].on && foes[i].y >= 0) kill_foe(&foes[i]);
    if (boss_on) { boss_hp -= 10; if (boss_hp <= 0) beat_boss(); }
}

static int near_ship(int x, int y) {
    return y == ship_y && x >= ship_x - 1 && x <= ship_x + 1;
}

static void hit_boss(void) {
    vx_tone(300, 40, 25, VX_NOISE, 0);
    if (--boss_hp <= 0) beat_boss();
}

static void aimed_shot(int x, int y) {
    int d = ship_x - x;
    add_bullet(x, y, d > 6 ? 2 : d > 2 ? 1 : d < -6 ? -2 : d < -2 ? -1 : 0);
}

static void update_boss(void) {
    int kind = (level - 1) % 10, w = W_->aw, h = W_->ah, angry = boss_hp * 2 < boss_max;
    int rate = 40 - level * 3;
    if (rate < 16) rate = 16;
    int mid = boss_x + w / 2, low = boss_y + h;
    boss_t++;
    if (warning) { warning--; return; }

    if (kind == 8 || (kind == 9 && boss_hp * 3 < boss_max * 2)) {   /* the laser bosses drift towards the ship */
        if (frame % (kind == 8 ? 4 : 5) == 0 && mid != ship_x) boss_x += mid < ship_x ? 1 : -1;
        if (boss_x < 1) boss_x = 1;
        if (boss_x > FW - w - 1) boss_x = FW - w - 1;
    } else if (kind == 4) {                       /* Oblivion jumps around the screen */
        if (boss_t % 70 == 0) { boss_x = 1 + vx_random(FW - w - 2); boss_y = 1 + vx_random(3); flash = 2; vx_tone(200, 120, 40, VX_SAW, 0); }
    } else if (frame % (kind == 2 ? 6 : 3) == 0) {
        boss_x += boss_dir;
        if (boss_x <= 1 || boss_x >= FW - w - 1) boss_dir = -boss_dir;
    }
    if (kind == 1 && frame % 8 == 0) boss_y = 2 + swing[boss_t / 8 & 15] / 6;   /* Inferno floats up and down */
    if (boss_y < 1) boss_y = 1;

    switch (kind) {
    case 0:                                /* Warden: twin guns */
        if (frame % rate == 0) {
            add_bullet(boss_x + 1, low, 0); add_bullet(boss_x + w - 2, low, 0);
            if (angry) add_bullet(mid, low, 0);
        }
        break;
    case 1:                                /* Inferno: a fan of fire */
        if (frame % (rate + 8) == 0) {
            for (int d = -2; d <= 2; d++) add_bullet(mid, low, d);
            if (angry) { add_bullet(mid - 4, low, -1); add_bullet(mid + 4, low, 1); }
            vx_tone(160, 120, 30, VX_SAW, 0);
        }
        break;
    case 2:                                /* Hive: hatches small foes */
        if (frame % (rate * 2) == 0)
            for (int i = 0, n = 0; i < FOES && n < (angry ? 3 : 2); i++)
                if (!foes[i].on) { spawn_foe(i, boss_x + 2 + n * (w - 4) / 2, low, 2 + n); n++; }
        if (frame % (rate + 12) == 0) aimed_shot(mid, low);
        break;
    case 3:                                /* Lance: warns, then fires a laser down one column */
        if (beam_t == 0 && frame % (angry ? 40 : 60) == 0) { beam_t = 45; beam_x = ship_x; vx_tone(1400, 400, 18, VX_TRIANGLE, 0); }
        if (beam_t) {
            if (--beam_t == 20) vx_tone(300, 300, 45, VX_SAW, 0);
            if (beam_t < 20 && ship_x >= beam_x - 1 && ship_x <= beam_x + 1) lose_life();
        }
        if (frame % (rate + 10) == 0) add_bullet(mid, low, boss_dir);
        break;
    case 4:                                /* Oblivion: aimed bursts, then everything at once */
        if (frame % rate == 0) { aimed_shot(boss_x + 2, low); aimed_shot(boss_x + w - 3, low); }
        if (angry && frame % (rate * 2) == 0) for (int d = -2; d <= 2; d++) add_bullet(mid, low, d);
        break;
    case 5:                                /* Prism: shards that bounce off the walls */
        if (frame % (rate + 4) == 0) {
            add_bullet_k(mid - 2, low, -2, 1); add_bullet_k(mid + 2, low, 2, 1);
            if (angry) { add_bullet_k(mid, low, -1, 1); add_bullet_k(mid, low, 1, 1); }
            vx_tone(1760, 60, 25, VX_TRIANGLE, 0);
        }
        break;
    case 6:                                /* Kraken: a curtain of snaking ink */
        if (frame % (rate + 6) == 0)
            for (int i = 0; i < (angry ? 5 : 4); i++) add_bullet_k(boss_x + 1 + i * (w - 2) / (angry ? 4 : 3), low, 0, 2);
        if (angry && frame % (rate * 2) == 0) aimed_shot(mid, low);
        break;
    case 7:                                /* Helios: a spinning sprinkler of fire */
        if (frame % (angry ? 3 : 5) == 0) {
            static const signed char dirs[8] = { -2, -1, 0, 1, 2, 1, 0, -1 };
            add_bullet(mid, low, dirs[spin & 7]);
            if (angry) add_bullet(mid, low, -dirs[spin & 7]);
            spin++;
        }
        break;
    default: {                             /* Sentinel and Overlord: a wide laser that follows the ship */
        int phase = kind == 8 ? 2 : boss_hp * 3 > boss_max * 2 ? 0 : boss_hp * 3 > boss_max ? 1 : 2;
        if (kind == 9) {
            if (frame % rate == 0) { aimed_shot(boss_x + 2, low); aimed_shot(boss_x + w - 3, low); }
            if (phase >= 1 && frame % (rate + 8) == 0) for (int d = -2; d <= 2; d++) add_bullet(mid, low, d);
            if (phase == 0 && frame % (rate * 2) == 0)
                for (int i = 0, n = 0; i < FOES && n < 2; i++)
                    if (!foes[i].on) { spawn_foe(i, boss_x + 2 + n * (w - 4), low, 3); n++; }
        } else if (frame % (rate + 10) == 0) aimed_shot(mid, low);
        if (phase == 2) {
            if (!track_t && frame % (kind == 8 ? 90 : 110) == 0) { track_t = 110; track_x = mid; vx_tone(900, 600, 20, VX_TRIANGLE, 0); }
            if (track_t) {
                track_t--;
                if (track_t < 80 && frame % 3 == 0 && track_x != ship_x) track_x += track_x < ship_x ? 1 : -1;
                if (track_t == 80) vx_tone(110, 1600, 50, VX_SAW, 0);
                if (track_t < 80 && track_t > 8 && ship_x >= track_x - 2 && ship_x <= track_x + 2) lose_life();
            }
        }
        break; }
    }
}

static void update_foes(void) {
    for (int i = 0; i < FOES; i++) {
        foe *f = &foes[i];
        if (!f->on) continue;
        int k = f->kind;
        if (frame % (k == 5 && f->y >= 3 ? 2 : 7) == 0) f->y++;
        int t = (int)(frame - f->born) / 4 + i;
        int amount = k == 1 ? swing[t & 15] : k == 2 ? swing[t & 15] / 2 : k == 3 ? ((t & 8) ? 18 : -18) : 0;
        if (frame % 4 == 0) {
            f->x += amount / 6;
            if (k == 5 && f->y >= 3 && f->x != ship_x) f->x += f->x < ship_x ? 1 : -1;
            if (f->x < 1) f->x = 1;
            if (f->x > FW - 2) f->x = FW - 2;
        }
        if (f->y >= 0 && vx_random(220 - (level > 8 ? 80 : level * 10)) == 0) {
            if (level >= 3 && (i & 1)) aimed_shot(f->x, f->y + 1); else add_bullet(f->x, f->y + 1, 0);
        }
        if (f->y >= 0 && near_ship(f->x, f->y)) { lose_life(); f->on = 0; if (!boss_on) wave_alive--; }
        else if (f->y >= FH) { f->on = 0; if (!boss_on) wave_alive--; }
    }
}

static void update_wave(void) {
    if (banner) { banner--; return; }
    if (!wave_alive) {
        if (--wave_pause > 0) return;
        wave_pause = 25;
        if (wave >= 5) start_boss(); else start_wave();
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
        if (s->on && boss_on && s->x >= boss_x && s->x < boss_x + W_->aw && s->y >= boss_y && s->y < boss_y + W_->ah) {
            s->on = 0;
            hit_boss();
        }
    }
    if (frame % 3 == 0)
        for (int i = 0; i < BULLETS; i++) {
            dot *b = &bullets[i];
            if (!b->on) continue;
            if (b->dx && (frame % 6 == 0 || b->dx > 1 || b->dx < -1)) b->x += b->dx > 0 ? 1 : -1;
            if (b->k == 1 && (b->x <= 0 || b->x >= FW - 1)) { b->dx = -b->dx; b->x = b->x <= 0 ? 1 : FW - 2; }
            if (b->k == 2 && frame % 6 == 0) b->x += (b->y / 3) & 1 ? 1 : -1;
            if (++b->y >= FH || b->x < 0 || b->x >= FW) b->on = 0;
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
            vx_tone(988, 60, 40, VX_TRIANGLE, 0); vx_tone(1319, 90, 40, VX_TRIANGLE, 60);
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
        if ((vx_held(K_UP) || vx_held(K_W)) && ship_y > FH / 2) ship_y--;
        if ((vx_held(K_DOWN) || vx_held(K_S)) && ship_y < FH - 1) ship_y++;
    }
    if (vx_held(K_SPACE)) fire();

    for (int i = 0; i < STARS; i++)
        if (frame % (3 - stars[i].on) == 0 && ++stars[i].y >= FH) { stars[i].y = 0; stars[i].x = vx_random(FW); }

    if (boss_on) update_boss(); else update_wave();
    update_foes();
    update_shots();
    update_items();
}

static void centre(int y, const char *s, uint8_t fg) {
    int n = (int)strlen(s), x = FW / 2 - n / 2;
    for (; *s; s++) vx_paint(x++, y, *s, fg, VC_BLACK);
}

static void draw(int state) {
    uint8_t text = VX_ATTR(VC_BLACK, VC_GLOW), dim = VX_ATTR(VC_BLACK, VC_MID);
    uint8_t ship = VX_ATTR(VC_BLACK, shield_time ? VC_GLOW : VC_NEON);
    vx_clear(flash ? VX_ATTR(VC_SEL, VC_GLOW) : dim);

    vx_text(0, 0, vx_english() ? "Score" : "Punkte", dim);    vx_number(7, 0, score, text);
    vx_text(16, 0, vx_english() ? "Best" : "Rekord", dim);    vx_number(23, 0, best, text);
    vx_text(32, 0, "L", dim);  vx_number(33, 0, lives, text);
    vx_text(36, 0, "$", dim);  vx_number(37, 0, coins, text);
    vx_text(41, 0, "X", dim);  vx_number(42, 0, shields, text);
    vx_text(45, 0, "C", dim);  vx_number(46, 0, novas, text);
    vx_text(49, 0, "Lv", dim); vx_number(51, 0, level, text); vx_text(level >= 10 ? 53 : 52, 0, "/10", dim);

    for (int i = 0; i < STARS; i++)
        vx_paint(stars[i].x, stars[i].y + 1, stars[i].on == 2 ? '.' : (char)0xFA, stars[i].on == 2 ? W_->star : W_->star2, VC_BLACK);
    for (int i = 0; i < ITEMS; i++)
        if (items[i].on) vx_put(items[i].x, items[i].y + 1, items[i].kind, VX_ATTR(VC_RAISE, VC_GLOW));
    for (int i = 0; i < FOES; i++)
        if (foes[i].on && foes[i].y >= 0) {
            int b = foes[i].kind & 1;
            vx_paint(foes[i].x, foes[i].y + 1, b ? W_->glyph_b : W_->glyph_a, b ? W_->foe_b : W_->foe_a, VC_BLACK);
        }
    if (boss_on && beam_t)
        for (int y = boss_y + W_->ah; y < FH; y++) {
            if (beam_t < 20) { for (int d = -1; d <= 1; d++) vx_paint(beam_x + d, y + 1, (char)0xDB, d ? 159 : 231, VC_BLACK); }
            else if (y % 2) vx_paint(beam_x, y + 1, (char)0xB3, 24, VC_BLACK);
        }
    if (boss_on && track_t)
        for (int y = boss_y + W_->ah; y < FH; y++)
            for (int d = -2; d <= 2; d++) {
                if (track_t >= 80) { if (d == 0 && y % 2) vx_paint(track_x, y + 1, (char)0xB3, 88, VC_BLACK); }
                else vx_paint(track_x + d, y + 1, (char)(d == -2 || d == 2 ? 0xB1 : 0xDB), d == 0 ? 231 : d == -2 || d == 2 ? 160 : 203, VC_BLACK);
            }
    for (int i = 0; i < BULLETS; i++)
        if (bullets[i].on) vx_paint(bullets[i].x, bullets[i].y + 1, bullets[i].k == 1 ? (char)0x04 : bullets[i].k == 2 ? '~' : '*', W_->bullet, VC_BLACK);
    for (int i = 0; i < SHOTS; i++)
        if (shots[i].on) vx_put(shots[i].x, shots[i].y + 1, (char)0xB3, text);
    if (boss_on) {
        for (int y = 0; y < W_->ah; y++)
            for (int x = 0; W_->art[y][x]; x++)
                if (W_->art[y][x] != ' ') vx_paint(boss_x + x, boss_y + y + 1, W_->art[y][x], flash ? 231 : W_->boss, VC_BLACK);
        for (int i = 0; i < 20; i++) vx_paint(FW / 2 - 10 + i, 1, (char)(i * boss_max < boss_hp * 20 ? 0xDB : 0xB0), W_->boss, VC_BLACK);
    }
    if (!hurt_time || (frame & 4)) {
        vx_text(ship_x - 1, ship_y + 1, "/A\\", ship);
        if (shield_time) { vx_put(ship_x - 2, ship_y + 1, '(', text); vx_put(ship_x + 2, ship_y + 1, ')', text); }
    }

    if (banner && state == 0) {
        char b[12] = "Level ";
        int n = 6, v = level;
        if (v >= 100) v = 99;
        if (v >= 10) b[n++] = (char)('0' + v / 10);
        b[n++] = (char)('0' + v % 10);
        b[n] = 0;
        centre(FH / 2 - 1, b, W_->hud);
        centre(FH / 2 + 1, W_->name, W_->foe_a);
    }
    if (warning && state == 0 && (warning & 8)) {
        centre(FH / 2 - 1, vx_english() ? "WARNING" : "WARNUNG", 196);
        centre(FH / 2 + 1, W_->boss_name, W_->boss);
    }
    if (state == 3) {
        static const uint8_t glow[6] = { 226, 220, 214, 208, 214, 220 };
        centre(FH / 2 - 3, vx_english() ? "VICTORY!" : "GESCHAFFT!", glow[(frame / 3) % 6]);
        centre(FH / 2 - 1, vx_english() ? "All ten worlds are free" : "Alle zehn Welten sind befreit", 231);
        char b[24] = "", *p = b; long v = score; char d[12]; int n = 0;
        do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        const char *t = vx_english() ? "Score " : "Punkte "; while (*t) *p++ = *t++;
        while (n) *p++ = d[--n]; *p = 0;
        centre(FH / 2 + 1, b, W_->hud);
        centre(FH / 2 + 3, vx_english() ? "Enter = play again" : "Enter = nochmal", 141);
    }
    if (state == 1) vx_text(FW / 2 - 3, FH / 2, "Pause", text);
    if (state == 2) {
        vx_text(FW / 2 - 6, FH / 2 - 1, vx_english() ? "Game over" : "Spiel vorbei", text);
        vx_text(FW / 2 - 9, FH / 2 + 1, vx_english() ? "Enter = play again" : "Enter = nochmal", dim);
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
                if (key == 'm' || key == 'M') chip_off = !chip_off;
                if ((key == 'x' || key == 'X') && shields && !shield_time) { shields--; shield_time = 75; vx_tone(523, 200, 30, VX_TRIANGLE, 0); }
                if ((key == 'c' || key == 'C') && lives > 0 && !paused) use_nova();
                if (key == VK_ENTER && (lives <= 0 || won)) { chip_cur = 0; new_game(); }
            }
        }
        fit_window();
        if (won) { frame++; chip_tick(); for (int i = 0; i < STARS; i++) if (++stars[i].y >= FH) { stars[i].y = 0; stars[i].x = vx_random(FW); } }
        else if (lives > 0 && !paused) { update(); chip_tick(); }
        else if (lives <= 0 && chip_cur) { chip_play(0); for (int i = 0; i < 5; i++) vx_tone(330 - i * 40, 200, 45, VX_SAW, i * 190); }
        if (score > best) { best = score; vx_write("best", &best, sizeof best); }
        draw(won ? 3 : lives <= 0 ? 2 : paused ? 1 : 0);
    }
}
