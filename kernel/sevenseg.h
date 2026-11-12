/* The seven-segment clock, shared by the screen saver in the kernel and the Clock program in
 * ring 3. Draws into a picture of 256-colour pixels. */
#pragma once
typedef struct {
    u8 *b;
    int w, h;
} ssbuf;
static void ss_rect(ssbuf *s, int x, int y, int w, int h, u8 c) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i >= 0 && j >= 0 && i < s->w && j < s->h)
                s->b[j * s->w + i] = c;
}
static const u8 ss_map[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
/* one segment: beveled ends like on a real display */
static void ss_seg(ssbuf *s, int x, int y, int len, int th, int vert, u8 c) {
    for (int i = 0; i < len; i++) {
        int edge = i < th / 2 ? i : len - 1 - i < th / 2 ? len - 1 - i : th / 2;
        int w = edge * 2 + 1 > th ? th : edge * 2 + 1;
        int o = (th - w) / 2;
        if (vert)
            ss_rect(s, x + o, y + i, w, 1, c);
        else
            ss_rect(s, x + i, y + o, 1, w, c);
    }
}
static void ss_digit(ssbuf *s, int x, int y, int w, int h, int d, u8 on, u8 off) {
    int t = w / 5 < 2 ? 2 : w / 5, g = t / 3 + 1, hh = (h - t) / 2;
    u8 m = d < 0 ? 0 : ss_map[d];
    ss_seg(s, x + g, y, w - 2 * g, t, 0, m & 1 ? on : off);           /* a */
    ss_seg(s, x + w - t, y + g, hh - g, t, 1, m & 2 ? on : off);      /* b */
    ss_seg(s, x + w - t, y + hh + g, hh - g, t, 1, m & 4 ? on : off); /* c */
    ss_seg(s, x + g, y + 2 * hh, w - 2 * g, t, 0, m & 8 ? on : off);  /* d */
    ss_seg(s, x, y + hh + g, hh - g, t, 1, m & 16 ? on : off);        /* e */
    ss_seg(s, x, y + g, hh - g, t, 1, m & 32 ? on : off);             /* f */
    ss_seg(s, x + g, y + hh, w - 2 * g, t, 0, m & 64 ? on : off);     /* g */
}
/* width of the time for digits dw wide (without the date) */
static int ss_width(int dw) {
    int gap = dw / 4, th = dw / 5 < 2 ? 2 : dw / 5;
    return 4 * dw + 4 * gap + th + gap + dw + gap / 2;
}
/* hours, minutes and small seconds at x, y; with date, the date in small digits below */
static void ss_clock(ssbuf *s, const rtc_t *t, int x, int y, int dw, int date, u8 on, u8 off,
                     u8 dim) {
    int dh = dw * 2, sw = dw / 2, sh = dh / 2, gap = dw / 4;
    ss_digit(s, x, y, dw, dh, t->h / 10, on, off);
    ss_digit(s, x + dw + gap, y, dw, dh, t->h % 10, on, off);
    int cx = x + 2 * dw + 2 * gap, th = dw / 5 < 2 ? 2 : dw / 5;
    u8 col = t->s % 2 ? off : on; /* the colon blinks */
    ss_rect(s, cx, y + dh / 3 - th / 2, th, th, col);
    ss_rect(s, cx, y + dh * 2 / 3 - th / 2, th, th, col);
    int mx2 = cx + th + gap;
    ss_digit(s, mx2, y, dw, dh, t->m / 10, on, off);
    ss_digit(s, mx2 + dw + gap, y, dw, dh, t->m % 10, on, off);
    int sx = mx2 + 2 * dw + 2 * gap;
    ss_digit(s, sx, y + dh - sh, sw, sh, t->s / 10, dim, off);
    ss_digit(s, sx + sw + gap / 2, y + dh - sh, sw, sh, t->s % 10, dim, off);
    if (!date)
        return;
    int qw = dw / 3, qh = qw * 2, qg = qw / 3, qx = x, qy = y + dh + dh / 4;
    int v[8] = {t->day / 10, t->day % 10,          -2,           t->mon / 10, t->mon % 10,
                -2,          (t->year / 10) % 10, t->year % 10};
    for (int i = 0; i < 8; i++) {
        if (v[i] == -2)
            ss_rect(s, qx, qy + qh - 2, 2, 2, dim);
        else
            ss_digit(s, qx, qy, qw, qh, v[i], dim, date);
        qx += v[i] == -2 ? qg + 3 : qw + qg;
    }
}
