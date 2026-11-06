/* Small chiptune player for Violyx programs.
   SPDX-License-Identifier: MIT
   A song has up to three parts written as text, one token per step:
     lead, bass: note and octave like c4 f#3 bb2, "." is a rest, "-" holds the note
     drums:      one letter per step: k kick, s snare, h hi-hat, . rest
   Call chip_play() once and chip_tick() in the game loop. */
#ifndef VX_CHIP_H
#define VX_CHIP_H
#include "violyx.h"
typedef struct { const char *lead, *bass, *drums; int step_ms; } chip_song;
static const chip_song *chip_cur;
static const char *chip_p[2], *chip_d;
static long chip_next;
static int chip_off;

static int chip_hz(const char *t) {   /* note token to Hz, using a table of the top octave */
    static const int oct8[12] = { 4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902 };
    static const signed char base[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* a b c d e f g */
    int n = base[t[0] - 'a']; t++;
    if (*t == '#') { n++; t++; } else if (*t == 'b') { n--; t++; }
    int o = *t - '0';
    if (n < 0) { n += 12; o--; } if (n > 11) { n -= 12; o++; }
    return o > 8 || o < 0 ? 440 : oct8[n] >> (8 - o);
}
static const char *chip_skip(const char *s) { while (*s && *s != ' ') s++; while (*s == ' ') s++; return s; }

static void chip_part(int i, const char *start, int vol, int wave, int delay) {
    const char *s = chip_p[i];
    if (!s) return;
    if (!*s) s = start;
    const char *n = chip_skip(s);
    if (*s >= 'a' && *s <= 'g') {
        int len = 1;
        const char *q = n;
        while (*q == '-') { len++; q = chip_skip(q); }
        vx_tone(chip_hz(s), len * chip_cur->step_ms - 12, vol, wave, delay);
    }
    chip_p[i] = n;
}
static void chip_step(int delay) {
    const chip_song *c = chip_cur;
    chip_part(0, c->lead, 26, VX_SQUARE, delay);
    chip_part(1, c->bass, 38, VX_TRIANGLE, delay);
    if (c->drums) {
        if (!*chip_d) chip_d = c->drums;
        char d = *chip_d++;
        if (d == 'k') vx_tone(70, 70, 42, VX_TRIANGLE, delay);
        if (d == 's') vx_tone(1600, 70, 18, VX_NOISE, delay);
        if (d == 'h') vx_tone(6000, 24, 7, VX_NOISE, delay);
    }
}
static void chip_play(const chip_song *c) {
    if (c == chip_cur) return;
    chip_cur = c; chip_p[0] = c ? c->lead : 0; chip_p[1] = c ? c->bass : 0; chip_d = c ? c->drums : 0;
    chip_next = vx_ms();
}
/* Plans notes a little ahead so the rhythm stays even when frames vary */
static void chip_tick(void) {
    long now = vx_ms();
    if (!chip_cur || chip_off) { chip_next = now; return; }
    if (chip_next < now - 200) chip_next = now;
    while (chip_next - now < 90) {
        chip_step((int)(chip_next > now ? chip_next - now : 0));
        chip_next += chip_cur->step_ms;
    }
}
#endif
