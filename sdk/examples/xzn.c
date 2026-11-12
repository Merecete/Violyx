/* XZN for Violyx - a keyboard music player, the Violyx edition of Xenon-Zones.
   SPDX-License-Identifier: MIT
   Plays MP3/MP2, OGG, FLAC and WAV, and with xzncodec.h AIFF, CAF/ALAC, WavPack, True Audio,
   DSD, Matroska audio and AC3. Files come from the system, USB sticks or CDs.
   The player runs on in the background while other windows are on top.
   Space play/pause, Left/Right seek, Up/Down volume, N next, P previous, S stop, M mute,
   R repeat, Z shuffle, B/V balance, E equalizer, C colours, W mini player, I info,
   A add files, Tab playlist, F1 or H shows all keys. */
#include "violyx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* ---------- memory: one arena per track, reset when the next track starts ---------- */
static char *hb, *hp, *he;
#ifndef HOSTTEST
void *malloc(size_t n) {
    n = (n + 31) & ~(size_t)15;
    while ((size_t)(he - hp) < n) {
        size_t g = n > (1 << 22) ? n : (1 << 22);
        char *p = vx_alloc(g);
        if (!p)
            return 0;
        if (!hb)
            hb = hp = p;
        he = p + ((g + 4095) & ~(size_t)4095);
    }
    size_t *b = (size_t *)hp;
    *b = n;
    hp += n;
    return b + 2;
}
void free(void *p) {
    (void)p;
}
void *realloc(void *p, size_t n) {
    if (!p)
        return malloc(n);
    size_t o = ((size_t *)p)[-2] - 16;
    if (n <= o)
        return p;
    void *q = malloc(n);
    if (q)
        memcpy(q, p, o);
    return q;
}
void qsort(void *b, size_t n, size_t s,
           int (*c)(const void *, const void *)) { /* few items, called while opening */
    char *a = b, t[64];
    if (s > 64)
        return;
    for (size_t i = 1; i < n; i++) {
        memcpy(t, a + i * s, s);
        size_t j = i;
        while (j && c(a + (j - 1) * s, t) > 0) {
            memcpy(a + j * s, a + (j - 1) * s, s);
            j--;
        }
        memcpy(a + j * s, t, s);
    }
}
#endif
/* ---------- decoders ---------- */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_MAX_CHANNELS 8
#define get_bits                vb_get_bits
#include "stb_vorbis.c"
#undef get_bits
#undef L
#undef R
#undef C
#undef M
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_WCHAR
#define DRFLAC_ASSERT(x) ((void)0)
#include "dr_flac.h"
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#define STBI_NO_BMP
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 4096
#define STBI_ASSERT(x)      ((void)0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "xzncodec.h" /* AIFF, CAF/ALAC, True Audio, WavPack, DSD, Matroska, AC3 */
#include "xznlogo.h"

__asm__(
    ".section .rodata\n.globl intro_mp3\n.globl intro_end\nintro_mp3:\n.incbin \"assets/xzn-intro.mp3\"\nintro_end:\n.text\n");
extern const unsigned char intro_mp3[], intro_end[];

/* ---------- system calls for audio ---------- */
static long au_free(void) {
    return vx_sys(22, 0, 0, 0);
}
static long au_write(const short *b, long n) {
    return vx_sys(22, (long)b, n, 0);
}
static void au_flush(void) {
    vx_sys(23, 0, 0, 0);
}
static long au_pos(void) {
    return vx_sys(23, 1, 0, 0);
}
static void au_gain(int l, int r) {
    vx_sys(23, 6, l, r);
}
static void au_pause(int p) {
    vx_sys(23, 7, p, 0);
}

/* ---------- playlist and state ---------- */
#define MAXT  1000
#define PATHN 150
#define NAMEN 70
typedef struct {
    char path[PATHN], name[NAMEN];
    int secs;
} track;
static track list[MAXT];
static int nt, cur = -1, sel, top_, shown = -1;
static long resume_ms; /* where the remembered track goes on after starting XZN */
static int order[MAXT];
enum {
    F_MP3 = 1,
    F_OGG,
    F_FLAC,
    F_WAV,
    F_GEN /* one of xzncodec.h */
};
typedef struct {
    int vol, bal, mute, rep, shuf, scheme, cur, pos, eqon, preset, mini;
    float eq[10], pre;
} saved;
static saved st = {80, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, {0}, 0};
static int playing, paused, fmt, srate, chans, kbps, focus_list, overlay;
enum {
    O_NONE,
    O_EQ,
    O_HELP,
    O_INFO,
    O_ADD,
    O_FIND,
    O_ABOUT
};
static const char *errmsg = 0;
static char title[140], artist[70], song[70];
static long total, dec_pos;   /* source frames */
static long t_start, base_ms; /* mix position where the track began, and its offset */
static long written, flushed; /* frames written to the kernel */
static int pend = -1;
static long pend_at, pend_base;
static const unsigned char *file;
static long flen;

/* mp3 */
static mp3dec_t mp3;
static long mp3_off;
static int *mp3_seek;
static int mp3_nseek;
static short mp3_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int mp3_have, mp3_at;
/* others */
static stb_vorbis *vb;
static drflac *fl;
static long wav_off, wav_end;
static int wav_bits;
static xdec gd; /* the other formats */
static int mp3_layer;
/* DSD is read in parts: a window of the file */
static char gd_path[PATHN];
static unsigned char *gd_win;
static long gd_wo = -1, gd_wn;
static long gd_fetch(xdec *d, void *b, long off, long len) {
    (void)d;
    if (!gd_win && !(gd_win = malloc(1 << 18)))
        return 0;
    if (off < gd_wo || off + len > gd_wo + gd_wn) {
        gd_wo = off & ~4095L;
        gd_wn = 0;
        while (gd_wn < (1 << 18)) {
            long k = vx_read_at(gd_path, gd_win + gd_wn, (unsigned long)(gd_wo + gd_wn),
                                (size_t)((1 << 18) - gd_wn));
            if (k <= 0)
                break;
            gd_wn += k;
        }
        if (off < gd_wo || off >= gd_wo + gd_wn)
            return 0;
    }
    long k = gd_wo + gd_wn - off;
    if (k > len)
        k = len;
    memcpy(b, gd_win + (off - gd_wo), (size_t)k);
    return k;
}

static void note_title(void);
static int mp3_open(void) {
    long o = 0;
    if (flen > 10 && !memcmp(file, "ID3", 3))
        o = 10 + ((file[6] & 127) << 21 | (file[7] & 127) << 14 | (file[8] & 127) << 7 |
                  (file[9] & 127));
    if (o >= flen)
        o = 0;
    mp3dec_init(&mp3);
    mp3_off = o;
    mp3_nseek = 0;
    mp3_seek = malloc(sizeof(int) * 14400);
    long frames = 0, next = 0, bytes = 0, n = 0;
    mp3dec_frame_info_t in;
    srate = 0;
    while (o < flen) {
        int s =
            mp3dec_decode_frame(&mp3, file + o, (int)(flen - o > 16384 ? 16384 : flen - o), 0, &in);
        if (!in.frame_bytes)
            break;
        if (s) {
            if (!srate) {
                srate = in.hz;
                chans = in.channels;
                mp3_layer = in.layer;
            }
            if (frames >= next && mp3_nseek < 14400) {
                mp3_seek[mp3_nseek++] = (int)o;
                next += srate / 2;
            }
            frames += s;
            bytes += in.frame_bytes;
            n++;
        }
        o += in.frame_bytes;
    }
    if (!srate)
        return 0;
    total = frames;
    kbps = frames ? (int)(bytes * 8 / (frames * 1000 / srate + 1)) : 0;
    mp3dec_init(&mp3);
    mp3_have = mp3_at = 0;
    return 1;
}
static int mp3_read(short *o, int n) {
    int g = 0;
    while (g < n) {
        if (mp3_at >= mp3_have) {
            mp3dec_frame_info_t in;
            if (mp3_off >= flen)
                break;
            long left = flen - mp3_off;
            int s = mp3dec_decode_frame(&mp3, file + mp3_off, (int)(left > 16384 ? 16384 : left),
                                        mp3_pcm, &in);
            if (!in.frame_bytes)
                break;
            mp3_off += in.frame_bytes;
            mp3_have = s;
            mp3_at = 0;
            if (in.channels == 1)
                for (int i = s - 1; i >= 0; i--) {
                    mp3_pcm[i * 2 + 1] = mp3_pcm[i];
                    mp3_pcm[i * 2] = mp3_pcm[i];
                }
            continue;
        }
        o[g * 2] = mp3_pcm[mp3_at * 2];
        o[g * 2 + 1] = mp3_pcm[mp3_at * 2 + 1];
        mp3_at++;
        g++;
    }
    return g;
}
static int wav_open(void) {
    if (flen < 44 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4))
        return 0;
    long o = 12;
    int ok = 0;
    while (o + 8 <= flen) {
        long l = file[o + 4] | file[o + 5] << 8 | file[o + 6] << 16 | (long)file[o + 7] << 24;
        if (l < 0)
            return 0;
        if (!memcmp(file + o, "fmt ", 4) && l >= 16 && o + 24 <= flen) {
            int tag = file[o + 8] | file[o + 9] << 8;
            chans = file[o + 10];
            srate = file[o + 12] | file[o + 13] << 8 | file[o + 14] << 16;
            wav_bits = file[o + 22];
            if ((tag != 1 && tag != 0xFFFE) || chans < 1 || chans > 2 ||
                (wav_bits != 8 && wav_bits != 16 && wav_bits != 24) || srate < 4000)
                return 0;
            ok = 1;
        }
        if (!memcmp(file + o, "data", 4)) {
            if (!ok)
                return 0;
            wav_off = o + 8;
            wav_end = wav_off + l;
            if (wav_end > flen)
                wav_end = flen;
            total = (wav_end - wav_off) / (chans * wav_bits / 8);
            kbps = srate * chans * wav_bits / 1000;
            return 1;
        }
        o += 8 + l + (l & 1);
    }
    return 0;
}
static int wav_read(short *o, int n) {
    int bs = wav_bits / 8, g = 0;
    for (; g < n && wav_off + bs * chans <= wav_end; g++) {
        for (int c = 0; c < 2; c++) {
            const unsigned char *p = file + wav_off + (c < chans ? c : 0) * bs;
            o[g * 2 + c] =
                bs == 1 ? (short)((p[0] - 128) << 8) : (short)(p[bs - 2] | p[bs - 1] << 8);
        }
        wav_off += bs * chans;
    }
    return g;
}
static int read_src(short *o, int n) {
    switch (fmt) {
        case F_MP3:
            return mp3_read(o, n);
        case F_OGG:
            return stb_vorbis_get_samples_short_interleaved(vb, 2, o, n * 2);
        case F_FLAC: {
            static short t[8192 * 8];
            int ch = chans > 8 ? 8 : chans, k = n > 8192 ? 8192 : n;
            int g = (int)drflac_read_pcm_frames_s16(fl, (drflac_uint64)k, t);
            for (int i = 0; i < g; i++) {
                o[i * 2] = t[i * chans];
                o[i * 2 + 1] = t[i * chans + (ch > 1)];
            }
            return g;
        }
        case F_WAV:
            return wav_read(o, n);
        case F_GEN:
            return gd.read(&gd, o, n);
    }
    return 0;
}
static void seek_src(long fr) {
    if (fr < 0)
        fr = 0;
    if (total && fr >= total)
        fr = total - 1;
    switch (fmt) {
        case F_MP3: {
            long i = srate ? fr / (srate / 2) : 0;
            if (i >= mp3_nseek)
                i = mp3_nseek - 1;
            if (i < 0)
                i = 0;
            mp3_off = mp3_seek[i];
            mp3dec_init(&mp3);
            mp3_have = mp3_at = 0;
            break;
        }
        case F_OGG:
            stb_vorbis_seek_frame(vb, (unsigned)fr);
            break;
        case F_FLAC:
            drflac_seek_to_pcm_frame(fl, (drflac_uint64)fr);
            break;
        case F_WAV:
            wav_off = (wav_off - wav_off) + 0;
            {
                long o = 12;
                while (o + 8 <= flen && memcmp(file + o, "data", 4)) {
                    long l = file[o + 4] | file[o + 5] << 8 | file[o + 6] << 16 |
                             (long)file[o + 7] << 24;
                    o += 8 + l + (l & 1);
                }
                wav_off = o + 8 + fr * chans * (wav_bits / 8);
            }
            break;
        case F_GEN:
            gd.seek(&gd, fr);
            break;
    }
    dec_pos = fr;
}

/* ---------- tags and cover ---------- */
#define COVER 64
static uint8_t cover[COVER * COVER];
static int has_cover;
static uint8_t rgb256(int r, int g, int b) {
    return vx_rgb(r, g, b);
}
static void to_ascii(const unsigned char *s, int n, char *o, int max) {
    int enc = n ? s[0] : 0, k = 0;
    s++;
    n--;
    int be = 0, i = 0;
    if ((enc == 1 || enc == 2) && n >= 2) {
        if (enc == 1) {
            be = s[0] == 0xFE;
            i = 2;
        } else
            be = 1;
        for (; i + 1 < n && k < max - 1; i += 2) {
            int c = be ? s[i] << 8 | s[i + 1] : s[i + 1] << 8 | s[i];
            if (!c)
                break;
            o[k++] = c < 127     ? (char)c
                     : c == 0xE4 ? (char)0x84
                     : c == 0xF6 ? (char)0x94
                     : c == 0xFC ? (char)0x81
                     : c == 0xC4 ? (char)0x8E
                     : c == 0xD6 ? (char)0x99
                     : c == 0xDC ? (char)0x9A
                     : c == 0xDF ? (char)0xE1
                                 : '?';
        }
    } else
        for (; i < n && k < max - 1 && s[i]; i++) {
            int c = s[i];
            if (enc == 3 && c >= 0xC0 && i + 1 < n) {
                c = (c & 31) << 6 | (s[++i] & 63);
            } else if (enc == 3 && c >= 0x80)
                c = '?';
            o[k++] = c < 127     ? (char)c
                     : c == 0xE4 ? (char)0x84
                     : c == 0xF6 ? (char)0x94
                     : c == 0xFC ? (char)0x81
                     : c == 0xC4 ? (char)0x8E
                     : c == 0xD6 ? (char)0x99
                     : c == 0xDC ? (char)0x9A
                     : c == 0xDF ? (char)0xE1
                                 : '?';
        }
    o[k] = 0;
    while (k && o[k - 1] == ' ')
        o[--k] = 0;
}
static void make_cover(const unsigned char *p, int n) {
    int w, h, c;
    if (!stbi_info_from_memory(p, n, &w, &h, &c) || w < 8 || h < 8 || (long)w * h > 9000000L)
        return;
    uint8_t *im = stbi_load_from_memory(p, n, &w, &h, &c, 3);
    if (!im)
        return;
    for (int y = 0; y < COVER; y++)
        for (int x = 0; x < COVER; x++) {
            long sx = (long)x * w / COVER, sy = (long)y * h / COVER, e = (sy * w + sx) * 3;
            cover[y * COVER + x] = rgb256(im[e], im[e + 1], im[e + 2]);
        }
    has_cover = 1;
}
static void read_id3(void) {
    if (flen < 10 || memcmp(file, "ID3", 3))
        return;
    int ver = file[3];
    if (ver < 3 || ver > 4)
        return;
    long end = 10 + ((long)(file[6] & 127) << 21 | (file[7] & 127) << 14 | (file[8] & 127) << 7 |
                     (file[9] & 127)),
         o = 10;
    if (end > flen)
        end = flen;
    if (file[5] & 0x80)
        return; /* unsynchronised tags are rare, skip them */
    while (o + 10 < end && file[o]) {
        long l =
            ver == 4
                ? ((long)(file[o + 4] & 127) << 21 | (file[o + 5] & 127) << 14 |
                   (file[o + 6] & 127) << 7 | (file[o + 7] & 127))
                : ((long)file[o + 4] << 24 | file[o + 5] << 16 | file[o + 6] << 8 | file[o + 7]);
        const unsigned char *d = file + o + 10;
        if (l <= 0 || o + 10 + l > end)
            break;
        if (!memcmp(file + o, "TIT2", 4))
            to_ascii(d, (int)l, song, 70);
        if (!memcmp(file + o, "TPE1", 4))
            to_ascii(d, (int)l, artist, 70);
        if (!memcmp(file + o, "APIC", 4) && !has_cover) {
            long i = 1;
            int enc = d[0];
            while (i < l && d[i])
                i++;
            i += 2; /* mime end, picture type */
            if (enc == 1 || enc == 2) {
                while (i + 1 < l && (d[i] || d[i + 1]))
                    i += 2;
                i += 2;
            } else {
                while (i < l && d[i])
                    i++;
                i++;
            }
            if (i < l)
                make_cover(d + i, (int)(l - i));
        }
        o += 10 + l;
    }
}

/* ---------- sound path: resample to 48 kHz, equalizer, spectrum ---------- */
#define BLOCK 1024
#define FFTN  512
static const int bands[10] = {60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000};
static const char *const band_txt[10] = {"60", "170", "310", "600", "1k",
                                         "3k", "6k",  "12k", "14k", "16k"};
static const struct {
    const char *n;
    signed char g[10];
} presets[8] = {{"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
                {"Rock", {5, 4, 2, -1, -2, 1, 3, 5, 5, 5}},
                {"Pop", {-1, 2, 4, 5, 4, 1, -1, -1, 1, 2}},
                {"Jazz", {4, 3, 1, 2, -1, -1, 0, 2, 3, 4}},
                {"Classical", {4, 3, 2, 0, 0, 0, -2, -3, -3, -4}},
                {"Bass", {8, 7, 5, 2, 0, 0, 0, 0, 0, 0}},
                {"Treble", {0, 0, 0, 0, 0, 2, 4, 6, 7, 8}},
                {"Vocal", {-2, -1, 2, 4, 5, 4, 2, 0, -1, -2}}};
typedef struct {
    float b0, b1, b2, a1, a2, z[2][2];
    int on;
} biquad;
static biquad eqf[10];
static float preamp = 1;
static void eq_update(void) {
    preamp = st.eqon ? (float)pow(10.0, st.pre / 20.0) : 1;
    for (int i = 0; i < 10; i++) {
        biquad *b = &eqf[i];
        b->on = st.eqon && (st.eq[i] > 0.01f || st.eq[i] < -0.01f);
        if (!b->on)
            continue;
        double A = pow(10.0, st.eq[i] / 40.0), w = 2 * M_PI * bands[i] / 48000,
               al = sin(w) / (2 * 1.4), c = cos(w), a0 = 1 + al / A;
        b->b0 = (float)((1 + al * A) / a0);
        b->b1 = (float)(-2 * c / a0);
        b->b2 = (float)((1 - al * A) / a0);
        b->a1 = (float)(-2 * c / a0);
        b->a2 = (float)((1 - al / A) / a0);
    }
}
static float scope[FFTN];
static int scope_at;
static short src[BLOCK * 4 + 8];
static int src_n, src_i;
static unsigned src_frac;
static int src_eof;
static void reset_path(void) {
    src_n = src_i = 0;
    src_frac = 0;
    src_eof = 0;
}
static void next_track(int dir, int from_end);
/* Fills out with n frames at 48 kHz. Returns frames, 0 at the end of the file */
static int produce(short *out, int n) {
    unsigned step = (unsigned)(((unsigned long)srate << 16) / 48000);
    int g = 0;
    while (g < n) {
        if (src_i + 1 >= src_n) {
            if (src_n) {
                src[0] = src[(src_n - 1) * 2];
                src[1] = src[(src_n - 1) * 2 + 1];
            }
            int k = read_src(src + 2, BLOCK);
            dec_pos += k;
            if (k <= 0) {
                src_eof = 1;
                break;
            }
            src_n = k + 1;
            src_i = 0;
        }
        int a = src_i, fr = (int)(src_frac >> 2); /* src_frac is 16 bit */
        int l = (src[a * 2] * (16384 - fr) + src[a * 2 + 2] * fr) >> 14;
        int r = (src[a * 2 + 1] * (16384 - fr) + src[a * 2 + 3] * fr) >> 14;
        out[g * 2] = (short)l;
        out[g * 2 + 1] = (short)r;
        g++;
        src_frac += step;
        src_i += (int)(src_frac >> 16);
        src_frac &= 0xFFFF;
    }
    return g;
}
static void shape(short *b, int n) {
    int any = preamp != 1;
    for (int i = 0; i < 10; i++)
        any |= eqf[i].on;
    for (int i = 0; i < n; i++) {
        float m = (b[i * 2] + b[i * 2 + 1]) * 0.5f;
        scope[scope_at] = m;
        scope_at = (scope_at + 1) % FFTN;
    }
    if (!any)
        return;
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < n; i++) {
            float x = b[i * 2 + c] * preamp;
            for (int k = 0; k < 10; k++) {
                biquad *f = &eqf[k];
                if (!f->on)
                    continue;
                float y = f->b0 * x + f->z[c][0];
                f->z[c][0] = f->b1 * x - f->a1 * y + f->z[c][1];
                f->z[c][1] = f->b2 * x - f->a2 * y;
                x = y;
            }
            b[i * 2 + c] = x > 32000 ? 32000 : x < -32000 ? -32000 : (short)x;
        }
}
static void set_gain(void) {
    int v = st.mute ? 0 : st.vol * 256 / 100, l = v, r = v;
    if (st.bal > 0)
        l = v * (100 - st.bal) / 100;
    else
        r = v * (100 + st.bal) / 100;
    au_gain(l, r);
}

/* ---------- opening tracks ---------- */
static int ends(const char *s, const char *e) {
    int a = (int)strlen(s), b = (int)strlen(e);
    if (a < b)
        return 0;
    for (int i = 0; i < b; i++) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c != e[i])
            return 0;
    }
    return 1;
}
static int audio_name(const char *s) {
    static const char *const ex[] = {".mp3", ".mp2", ".mp1", ".mpa", ".ogg", ".oga", ".flac",
                                     ".wav", ".aif", ".aiff", ".aifc", ".caf", ".wv",  ".tta",
                                     ".dsf", ".dff", ".mka", ".ac3"};
    for (unsigned i = 0; i < sizeof ex / sizeof ex[0]; i++)
        if (ends(s, ex[i]))
            return 1;
    return 0;
}
static void base_name(const char *p, char *o, int n) {
    const char *b = p;
    for (const char *q = p; *q; q++)
        if (*q == '/')
            b = q + 1;
    int i = 0;
    for (; b[i] && i < n - 1; i++)
        o[i] = b[i];
    o[i] = 0;
}
static void save_list(void);
static void draw(long t);
static void numtxt(char *o, long v);
static int load_track(int i) {
    track *t = &list[i];
    hp = hb;
    vb = 0;
    fl = 0;
    fmt = 0;
    total = 0;
    dec_pos = 0;
    kbps = 0;
    has_cover = 0;
    artist[0] = song[0] = 0;
    errmsg = 0;
    memset(&gd, 0, sizeof gd);
    gd_wo = -1;
    gd_wn = 0;
    gd_win = 0;
    mp3_layer = 3;
    if (!strcmp(t->path, "*intro")) {
        file = intro_mp3;
        flen = intro_end - intro_mp3;
    } else if (ends(t->path, ".dsf") || ends(t->path, ".dff")) {
        /* DSD: only the start now, the rest while playing */
        vx_stat_t s;
        if (vx_stat(t->path, &s) < 0 || s.dir) {
            errmsg = vx_english() ? "File not found" : "Datei nicht gefunden";
            return 0;
        }
        xc_cpy(gd_path, t->path, PATHN);
        unsigned char *h = malloc(65536);
        long hn = h ? vx_read_at(t->path, h, 0, 65536) : 0;
        gd.fetch = gd_fetch;
        gd.n = (long)s.size;
        if (hn <= 0 || !dsd_open(&gd, h, hn)) {
            errmsg = vx_english() ? "Unsupported or damaged file" : "Datei defekt oder Format unbekannt";
            return 0;
        }
        file = 0;
        flen = (long)s.size;
        fmt = F_GEN;
        srate = gd.srate;
        chans = gd.chans;
        total = gd.total;
        kbps = gd.kbps;
        t->secs = srate ? (int)(total / srate) : 0;
        return 1;
    } else {
        vx_stat_t s;
        if (vx_stat(t->path, &s) < 0 || s.dir || s.size > (200u << 20)) {
            errmsg = vx_english() ? "File not found" : "Datei nicht gefunden";
            return 0;
        }
        char *b = malloc(s.size + 16);
        if (!b) {
            errmsg = vx_english() ? "File too large" : "Datei zu gross";
            return 0;
        }
        /* Read in parts: the music before, the visualizer and other programs keep running */
        static char msg[40];
        long n = 0, last = vx_ms();
        while (n < (long)s.size) {
            long k = vx_read_at(t->path, b + n, (unsigned long)n, s.size - n);
            if (k <= 0)
                break;
            n += k;
            if (vx_ms() - last > 60) {
                const char *l = vx_english() ? "Loading " : "Lade ";
                int m = 0;
                for (; l[m]; m++)
                    msg[m] = l[m];
                numtxt(msg + m, (long)((long long)n * 100 / (long long)s.size));
                m = (int)strlen(msg);
                msg[m++] = ' ';
                msg[m++] = '%';
                msg[m] = 0;
                errmsg = msg;
                draw(vx_ms());
                errmsg = 0;
                last = vx_ms();
            }
        }
        if (n <= 0 || n < (long)s.size) {
            free(b);
            errmsg = vx_english() ? "Cannot read file" : "Datei nicht lesbar";
            return 0;
        }
        file = (unsigned char *)b;
        flen = n;
    }
    int ok = 0;
    /* first the formats of xzncodec.h; Matroska may give back a stream for the others */
    gd.f = file;
    gd.n = flen;
    int gr = xd_open(&gd, t->path);
    if (gr == 2) {
        file = gd.f;
        flen = gd.n;
    }
    if (gd.title[0])
        xc_cpy(song, gd.title, sizeof song);
    if (gd.artist[0])
        xc_cpy(artist, gd.artist, sizeof artist);
    if (gr == 1) {
        fmt = F_GEN;
        srate = gd.srate;
        chans = gd.chans;
        total = gd.total;
        kbps = gd.kbps;
        ok = 1;
    } else if (flen > 4 && !memcmp(file, "OggS", 4)) {
        int e;
        vb = stb_vorbis_open_memory(file, (int)flen, &e, 0);
        if (vb) {
            stb_vorbis_info in = stb_vorbis_get_info(vb);
            srate = (int)in.sample_rate;
            chans = in.channels;
            total = stb_vorbis_stream_length_in_samples(vb);
            kbps = total ? (int)(flen * 8 / (total * 1000 / srate + 1)) : 0;
            fmt = F_OGG;
            ok = chans >= 1;
        }
    } else if (flen > 4 && !memcmp(file, "fLaC", 4)) {
        fl = drflac_open_memory(file, (size_t)flen, 0);
        if (fl) {
            srate = (int)fl->sampleRate;
            chans = fl->channels;
            total = (long)fl->totalPCMFrameCount;
            kbps = total ? (int)(flen * 8 / (total * 1000 / srate + 1)) : 0;
            fmt = F_FLAC;
            ok = 1;
        }
    } else if (flen > 4 && !memcmp(file, "RIFF", 4)) {
        ok = wav_open();
        if (ok)
            fmt = F_WAV;
    } else {
        read_id3();
        ok = mp3_open();
        if (ok)
            fmt = F_MP3;
    }
    if (!ok || srate < 4000 || srate > 192000) {
        fmt = 0;
        errmsg =
            vx_english() ? "Unsupported or damaged file" : "Datei defekt oder Format unbekannt";
        return 0;
    }
    t->secs = srate ? (int)(total / srate) : 0;
    return 1;
}
static void set_title(int i) {
    track *t = &list[i];
    if (song[0] && artist[0]) {
        int k = 0;
        for (const char *a = artist; *a && k < 60; a++)
            title[k++] = *a;
        title[k++] = ' ';
        title[k++] = '-';
        title[k++] = ' ';
        for (const char *a = song; *a && k < 130; a++)
            title[k++] = *a;
        title[k] = 0;
    } else if (song[0]) {
        memcpy(title, song, sizeof song);
        title[69] = 0;
    } else {
        int k = 0;
        for (; t->name[k]; k++)
            title[k] = t->name[k];
        title[k] = 0;
    }
}
/* Starts track i. gapless keeps what is already in the stream */
static void play(int i, long start_ms, int gapless) {
    resume_ms = 0;
    if (i < 0 || i >= nt)
        return;
    int prev_shown = shown;
    (void)prev_shown;
    cur = i;
    if (!load_track(i)) {
        playing = 0;
        shown = i;
        title[0] = 0;
        base_name(list[i].name, title, 70);
        if (!gapless)
            au_flush();
        return;
    }
    reset_path();
    for (int k = 0; k < 10; k++) {
        eqf[k].z[0][0] = eqf[k].z[0][1] = eqf[k].z[1][0] = eqf[k].z[1][1] = 0;
    }
    if (start_ms > 0)
        seek_src(start_ms / 1000 * srate);
    long now = au_pos();
    if (!gapless) {
        au_flush();
        au_pause(0);
        paused = 0;
        flushed = written = now;
        t_start = now;
        base_ms = start_ms;
        shown = i;
        set_title(i);
        pend = -1;
    } else {
        pend = i;
        pend_at = written;
        pend_base = 0;
    }
    playing = 1;
    st.cur = i;
    save_list();
}
static long now_ms(void) {
    if (!playing && !fmt)
        return 0;
    long p = au_pos() - t_start;
    if (p < 0)
        p = 0;
    return base_ms + p * 10 / 480;
}
static void seek_to(long ms) {
    if (!fmt)
        return;
    if (ms < 0)
        ms = 0;
    if (ms > total * 1000 / srate)
        ms = total * 1000 / srate;
    seek_src(ms / 100 * srate / 10);
    reset_path();
    au_flush();
    long now = au_pos();
    written = now;
    t_start = now;
    base_ms = ms;
    playing = 1;
    pend = -1;
}
static void stop_(void) {
    playing = 0;
    paused = 0;
    au_flush();
    au_pause(0);
    fmt = 0;
    base_ms = 0;
}
static int pick_next(int dir) {
    if (!nt)
        return -1;
    if (st.shuf)
        return vx_random(nt);
    int n = cur + dir;
    if (n >= nt)
        return st.rep ? 0 : -1;
    if (n < 0)
        return 0;
    return n;
}
static void next_track(int dir, int auto_) {
    int n = st.rep == 2 && auto_ ? cur : pick_next(dir);
    if (n < 0) {
        if (auto_)
            playing = 0;
        return;
    }
    play(n, 0, auto_);
    if (n >= top_ + 8)
        top_ = n - 2;
}
static void pump(void) {
    static short o[BLOCK * 2];
    for (int k = 0; k < 8 && playing; k++) {
        long inflight = written - au_pos();
        if (inflight > 48000 || au_free() < BLOCK)
            break;
        int n = produce(o, BLOCK);
        if (n > 0) {
            shape(o, n);
            au_write(o, n);
            written += n;
        }
        if (src_eof) {
            next_track(1, 1);
            break;
        }
    }
    if (pend >= 0 && au_pos() >= pend_at) { /* the new track is now audible */
        t_start = pend_at;
        base_ms = 0;
        shown = pend;
        set_title(pend);
        pend = -1;
    }
}

/* ---------- saving ---------- */
static void save_state(void) {
    st.pos = playing || fmt ? (int)(now_ms() / 1000) : (int)(resume_ms / 1000);
    vx_write("state", &st, sizeof st);
}
static char lb[MAXT * (PATHN + NAMEN + 12)];
static void save_list(void) {
    int o = 0;
    for (int i = 0; i < nt; i++) {
        track *t = &list[i];
        for (const char *s = t->path; *s; s++)
            lb[o++] = *s;
        lb[o++] = '\t';
        for (const char *s = t->name; *s; s++)
            lb[o++] = *s;
        lb[o++] = '\t';
        char d[12];
        int n = 0, v = t->secs;
        do {
            d[n++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        while (n)
            lb[o++] = d[--n];
        lb[o++] = '\n';
    }
    vx_write("playlist", lb, (size_t)o);
    save_state();
}
static void add_track(const char *path, const char *name) {
    if (nt >= MAXT || strlen(path) >= PATHN)
        return;
    for (int i = 0; i < nt; i++)
        if (!strcmp(list[i].path, path))
            return;
    track *t = &list[nt++];
    memcpy(t->path, path, strlen(path) + 1);
    int i = 0;
    for (; name[i] && i < NAMEN - 1; i++)
        t->name[i] = name[i];
    t->name[i] = 0;
    t->secs = 0;
}
static void load_list(void) {
    long n = vx_read("playlist", lb, sizeof lb - 1);
    nt = 0;
    if (n > 0) {
        lb[n] = 0;
        char *p = lb;
        while (*p && nt < MAXT) {
            char *e = p;
            while (*e && *e != '\n')
                e++;
            char *a = p;
            while (a < e && *a != '\t')
                a++;
            char *b = a + 1;
            while (b < e && *b != '\t')
                b++;
            if (a < e && b < e && a - p < PATHN && b - a - 1 < NAMEN) {
                track *t = &list[nt++];
                memcpy(t->path, p, (size_t)(a - p));
                t->path[a - p] = 0;
                memcpy(t->name, a + 1, (size_t)(b - a - 1));
                t->name[b - a - 1] = 0;
                t->secs = 0;
                for (char *q = b + 1; q < e && *q >= '0' && *q <= '9'; q++)
                    t->secs = t->secs * 10 + *q - '0';
            }
            p = *e ? e + 1 : e;
        }
    }
    if (!nt)
        add_track("*intro", "XZN Intro (Sample)");
}

/* ---------- colours, drawing ---------- */
typedef struct {
    const char *n;
    uint8_t bg, panel, raised, dim, text, hi, lcd, bar1, bar2, top;
} scheme;
static const scheme schemes[5] = {{"Violet", 233, 235, 238, 98, 189, 231, 141, 93, 141, 201},
                                  {"Amber", 233, 235, 238, 137, 223, 231, 215, 172, 215, 226},
                                  {"Cyan", 233, 235, 238, 67, 152, 231, 80, 31, 80, 123},
                                  {"Rose", 233, 235, 238, 132, 218, 231, 211, 161, 211, 225},
                                  {"Lime", 233, 235, 238, 71, 151, 231, 119, 34, 119, 193}};
static const scheme *S;
static int W, H;
static void fillr(int x, int y, int w, int h, uint8_t bg) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            vx_paint(x + i, y + j, ' ', S->text, bg);
}
static int tx(int x, int y, const char *s, uint8_t fg, uint8_t bg, int max) {
    int n = 0;
    for (; *s && n < max; s++, n++)
        vx_paint(x++, y, *s, fg, bg);
    return n;
}
static void mmss(char *o, long s) {
    if (s < 0)
        s = 0;
    if (s > 359999)
        s = 359999;
    long m = s / 60;
    if (m > 99) {
        o[0] = (char)('0' + m / 100);
        o++;
        m %= 100;
    }
    o[0] = (char)('0' + m / 10);
    o[1] = (char)('0' + m % 10);
    o[2] = ':';
    o[3] = (char)('0' + s % 60 / 10);
    o[4] = (char)('0' + s % 10);
    o[5] = 0;
}
static void numtxt(char *o, long v) {
    char d[12];
    int n = 0;
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *o++ = d[--n];
    *o = 0;
}
static int button(int x, int y, const char *s, int on) {
    int n = (int)strlen(s);
    vx_paint(x, y, ' ', S->hi, on ? S->bar2 : S->raised);
    tx(x + 1, y, s, on ? 16 : S->hi, on ? S->bar2 : S->raised, n);
    vx_paint(x + n + 1, y, ' ', S->hi, on ? S->bar2 : S->raised);
    return n + 3;
}
static void half(int x, int y2,
                 uint8_t c) { /* a pixel at half-cell height, drawn with the buffer of the row */
    static uint8_t row[240][100][2];
    if (x < 0 || x >= 240 || y2 < 0 || y2 >= 200)
        return;
    row[x][y2 / 2][y2 & 1] = c;
    vx_paint(x, y2 / 2, (char)0xDF, row[x][y2 / 2][0], row[x][y2 / 2][1]);
}

/* The banner is a real picture: 4x8 pixels per cell, so the XZN lettering stays smooth */
static uint8_t bimg[960 * 64];
static void idx_rgb(int i, int *r, int *g, int *b) {
    static const uint8_t L6[6] = {0, 95, 135, 175, 215, 255};
    if (i >= 232) {
        *r = *g = *b = 8 + 10 * (i - 232);
        return;
    }
    if (i < 16)
        i = 16;
    i -= 16;
    *r = L6[i / 36];
    *g = L6[i / 6 % 6];
    *b = L6[i % 6];
}
/* Four close shades per scheme, from dark to light, mixed with ordered dithering into a soft
 * gradient */
static const uint8_t grad[5][4] = {
    {54, 55, 91, 92}, {94, 130, 136, 172}, {23, 24, 30, 31}, {89, 125, 126, 162}, {22, 28, 34, 70}};
static const signed char bayer[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static int lv(int v) {
    return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40 > 5 ? 5 : (v - 35) / 40;
}
static uint8_t near_rgb(int r, int g, int b) {
    return (uint8_t)(16 + 36 * lv(r) + 6 * lv(g) + lv(b));
}
static void banner(int y, int h, long t) {
    int pw = W * 4, ph = h * 8;
    if (pw > 960)
        pw = 960;
    if (ph > 32)
        ph = 32;
    const uint8_t *g = grad[st.scheme];
    int sheen = (int)(t / 25) % (pw + 120) - 60; /* a soft light moves slowly across */
    for (int j = 0; j < ph; j++)
        for (int i = 0; i < pw; i++) {
            int f = (ph - 1 - j) * 400 / ph + i * 200 / pw; /* 0..600: light at the bottom right */
            int d = i - sheen - j / 2;
            if (d < 0)
                d = -d;
            if (d < 40)
                f += (40 - d) * 3;
            if (f > 599)
                f = 599;
            int k = f / 200, part = (f % 200) * 16 / 200;
            bimg[j * pw + i] = g[k + (part > bayer[j & 3][i & 3])];
        }
    int big = ph >= 32, lw = big ? LOGOB_W : LOGOS_W, lh = big ? LOGOB_H : LOGOS_H;
    const unsigned char *lp = big ? logoB_px : logoS_px;
    int ox = big ? 12 : 6, oy = (ph - lh) / 2;
    for (int j = 0; j < lh; j++)
        for (int i = 0; i < lw; i++) {
            int a = lp[j * lw + i], X = ox + i, Y = oy + j;
            if (!a || X >= pw || Y < 0 || Y >= ph)
                continue;
            int r, gg, b;
            idx_rgb(bimg[Y * pw + X], &r, &gg, &b);
            bimg[Y * pw + X] = a > 200
                                   ? 231
                                   : near_rgb(r + (255 - r) * a / 255, gg + (255 - gg) * a / 255,
                                              b + (255 - b) * a / 255);
        }
    if (has_cover) {
        int side = ph;
        for (int j = 0; j < side; j++)
            for (int i = 0; i < side; i++)
                bimg[j * pw + pw - side - 8 + i] =
                    cover[(j * COVER / side) * COVER + i * COVER / side];
    }
    if (overlay == O_ABOUT) { /* the logo for the Info window sits below the banner */
        for (int j = 0; j < 32; j++) {
            memset(bimg + (ph + j) * pw, 0, (size_t)pw);
            memcpy(bimg + (ph + j) * pw, logo_icon + j * 32, 32);
        }
        vx_image(bimg, pw, ph + 32);
    } else
        vx_image(bimg, pw, ph);
    for (int j = 0; j < ph / 8; j++)
        for (int i = 0; i < pw / 4; i++)
            vx_pixcell(i, y + j, i, j);
}
static void slider(int x, int y, int w, int v, int max, uint8_t fg) {
    int p = max ? v * w / max : 0;
    for (int i = 0; i < w; i++)
        vx_paint(x + i, y, (char)(i < p ? 0xDB : 0xB0), i < p ? fg : S->raised, S->panel);
}
/* FFT on the newest samples, drawn as bars */
static float bar[120], peak[120];
static void spectrum(int y, int h, int w, int bars) {
    static float re[FFTN], im[FFTN], win[FFTN];
    static int ready;
    if (!ready) {
        ready = 1;
        for (int i = 0; i < FFTN; i++)
            win[i] = (float)(0.5 - 0.5 * cos(2 * M_PI * i / FFTN));
    }
    for (int i = 0; i < FFTN; i++) {
        re[i] = scope[(scope_at + i) % FFTN] * win[i] / 32768.0f;
        im[i] = 0;
    }
    for (int i = 1, j = 0; i < FFTN; i++) {
        int b = FFTN >> 1;
        for (; j & b; b >>= 1)
            j ^= b;
        j ^= b;
        if (i < j) {
            float t = re[i];
            re[i] = re[j];
            re[j] = t;
        }
    }
    for (int l = 2; l <= FFTN; l <<= 1) {
        double a = -2 * M_PI / l;
        float wr = (float)cos(a), wi = (float)sin(a);
        for (int i = 0; i < FFTN; i += l) {
            float cr = 1, ci = 0;
            for (int k = 0; k < l / 2; k++) {
                int p = i + k, q = p + l / 2;
                float xr = re[q] * cr - im[q] * ci, xi = re[q] * ci + im[q] * cr;
                re[q] = re[p] - xr;
                im[q] = im[p] - xi;
                re[p] += xr;
                im[p] += xi;
                float t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
    if (bars > 120)
        bars = 120;
    for (int b = 0; b < bars; b++) {
        int a = 1 + (int)(pow(180.0, (double)b / bars)),
            e = 1 + (int)(pow(180.0, (double)(b + 1) / bars));
        if (e <= a)
            e = a + 1;
        float m = 0;
        for (int i = a; i < e && i < FFTN / 2; i++) {
            float v = re[i] * re[i] + im[i] * im[i];
            if (v > m)
                m = v;
        }
        float d = (float)((10 * log(m / (float)(FFTN * FFTN / 16) + 1e-9) / 2.302585 + 60) /
                          60); /* -60..0 dB */
        if (paused || !playing)
            d = 0;
        if (d < 0)
            d = 0;
        if (d > 1)
            d = 1;
        bar[b] = d > bar[b] ? d : bar[b] * 0.82f;
        peak[b] = bar[b] > peak[b] ? bar[b] : peak[b] - 0.03f;
    }
    int cw = w / bars;
    if (cw < 1)
        cw = 1;
    int px = h * 2;
    for (int b = 0; b < bars; b++) {
        int v = (int)(bar[b] * px + .5f), pk = (int)(peak[b] * px + .5f);
        for (int j = 0; j < px; j++) {
            int fromb = px - 1 - j;
            uint8_t c = fromb < v ? (fromb * 3 > px * 2 ? S->top
                                     : fromb * 3 > px   ? S->bar2
                                                        : S->bar1)
                                  : (fromb == pk && pk > 1 ? S->hi : S->panel);
            for (int k = 0; k < cw - (cw > 1); k++)
                half(b * cw + k, y * 2 + j, c);
            if (cw > 1)
                half(b * cw + cw - 1, y * 2 + j, S->panel);
        }
    }
}

/* ---------- picker for files ---------- */
static char pk_path[PATHN], pk_buf[32768];
static char *pk_it[1024];
static int pk_n, pk_sel, pk_top;
static void pk_load(void) {
    pk_n = pk_sel = pk_top = 0;
    if (!pk_path[0]) {
        static char *r[3] = {"home/", "downloads/", "drives/"};
        for (int i = 0; i < 3; i++)
            pk_it[pk_n++] = r[i];
        return;
    }
    long n = vx_list(pk_path, pk_buf, sizeof pk_buf);
    if (n <= 0)
        return;
    for (char *p = pk_buf; p < pk_buf + n && pk_n < 1024;) {
        int l = (int)strlen(p);
        if (p[l - 1] == '/' || audio_name(p))
            pk_it[pk_n++] = p;
        p += l + 1;
    }
}
static int join(char *o, const char *a, const char *b) {
    int n = 0;
    for (; *a && n < PATHN - 2; a++)
        o[n++] = *a;
    if (!n || o[n - 1] != '/')
        o[n++] = '/';
    for (; *b && n < PATHN - 1; b++)
        o[n++] = *b;
    if (n && o[n - 1] == '/')
        n--;
    o[n] = 0;
    return n;
}
static void add_folder(const char *dir, int depth) {
    static char buf[4][8192];
    if (depth > 3)
        return;
    long n = vx_list(dir, buf[depth], sizeof buf[depth]);
    if (n <= 0)
        return;
    for (char *p = buf[depth]; p < buf[depth] + n;) {
        int l = (int)strlen(p);
        char f[PATHN];
        join(f, dir, p);
        if (p[l - 1] == '/')
            add_folder(f, depth + 1);
        else if (audio_name(p)) {
            char nm[NAMEN];
            base_name(f, nm, NAMEN);
            add_track(f, nm);
        }
        p += l + 1;
    }
}
static void pk_key(int k) {
    int vis = H - 6;
    if (k == VK_UP && pk_sel)
        pk_sel--;
    if (k == VK_DOWN && pk_sel < pk_n - 1)
        pk_sel++;
    if (k == VK_PGUP) {
        pk_sel -= vis;
        if (pk_sel < 0)
            pk_sel = 0;
    }
    if (k == VK_PGDN) {
        pk_sel += vis;
        if (pk_sel >= pk_n)
            pk_sel = pk_n - 1;
        if (pk_sel < 0)
            pk_sel = 0;
    }
    if (pk_sel < pk_top)
        pk_top = pk_sel;
    if (pk_sel >= pk_top + vis)
        pk_top = pk_sel - vis + 1;
    if (k == VK_BS) {
        int l = (int)strlen(pk_path);
        while (l && pk_path[l - 1] != '/')
            l--;
        if (l)
            l--;
        pk_path[l] = 0;
        pk_load();
    }
    if ((k == 'f' || k == 'F') && pk_path[0]) { /* the whole folder shown right now */
        int was = nt;
        add_folder(pk_path, 0);
        if (nt != was) {
            save_list();
            if (!playing && !fmt)
                play(was, 0, 0);
        }
        return;
    }
    if (k == VK_ENTER && pk_n) {
        char f[PATHN], *it = pk_it[pk_sel];
        int l = (int)strlen(it);
        join(f, pk_path[0] ? pk_path : "/", it);
        if (it[l - 1] == '/') {
            memcpy(pk_path, f, PATHN);
            pk_load();
            return;
        }
        int was = nt;
        char nm[NAMEN];
        base_name(f, nm, NAMEN);
        add_track(f, nm);
        if (nt != was) {
            save_list();
            if (!playing && !fmt)
                play(was, 0, 0);
        }
    }
}
static void draw_picker(void) {
    int bw = W - 8, x = 4, y = 1, bh = H - 2;
    fillr(x, y, bw, bh, S->raised);
    tx(x + 2, y, vx_english() ? "ADD FILES" : "DATEIEN HINZUFUEGEN", S->hi, S->raised, bw);
    tx(x + 2, y + 1, pk_path[0] ? pk_path : "/", S->lcd, S->raised, bw - 4);
    fillr(x + 1, y + 2, bw - 2, bh - 4, S->bg);
    int vis = H - 6;
    if (!pk_n)
        tx(x + 3, y + 3, vx_english() ? "No music files here" : "Hier liegt keine Musik", S->dim,
           S->bg, bw - 6);
    for (int i = 0; i < vis && pk_top + i < pk_n; i++) {
        int s = pk_top + i == pk_sel;
        char *it = pk_it[pk_top + i];
        int d = it[strlen(it) - 1] == '/';
        if (s)
            fillr(x + 1, y + 2 + i, bw - 2, 1, S->raised);
        tx(x + 3, y + 2 + i, it, d ? S->lcd : S->text, s ? S->raised : S->bg, bw - 6);
    }
    tx(x + 2, y + bh - 1,
       vx_english() ? "Enter open/add  F add folder  Backspace up  Esc done"
                    : "Enter oeffnen/hinzu  F ganzer Ordner  Ruecktaste hoch  Esc fertig",
       S->dim, S->raised, bw - 4);
}
/* ---------- overlays ---------- */
static int eq_sel;
static void eq_preset(int p) {
    st.preset = p;
    for (int i = 0; i < 10; i++)
        st.eq[i] = presets[p].g[i];
    st.pre = 0;
    eq_update();
}
static void eq_key(int k) {
    if (k == VK_LEFT && eq_sel)
        eq_sel--;
    if (k == VK_RIGHT && eq_sel < 10)
        eq_sel++;
    float *v = eq_sel == 0 ? &st.pre : &st.eq[eq_sel - 1];
    if (k == VK_UP && *v < 12) {
        *v += 1;
        eq_update();
    }
    if (k == VK_DOWN && *v > -12) {
        *v -= 1;
        eq_update();
    }
    if (k == 'o' || k == 'O') {
        st.eqon = !st.eqon;
        eq_update();
    }
    if (k == 'r' || k == 'R')
        eq_preset(0);
    if (k == 't' || k == 'T' || k == VK_TAB)
        eq_preset((st.preset + 1) % 8);
    vx_write("state", &st, sizeof st);
}
static void draw_eq(void) {
    int bw = 60, bh = 18, x = (W - bw) / 2, y = (H - bh) / 2;
    if (y < 0)
        y = 0;
    fillr(x, y, bw, bh, S->raised);
    tx(x + 2, y + 1, "10-BAND EQUALIZER", S->lcd, S->raised, 30);
    tx(x + bw - 16, y + 1, "Preamp", S->dim, S->raised, 8);
    button(x + bw - 8, y + 1, st.eqon ? "ON" : "OFF", st.eqon);
    for (int c = 0; c < 11; c++) {
        int cx = x + 3 + c * 5 - (c ? 0 : 0);
        float v = c ? st.eq[c - 1] : st.pre;
        int on = eq_sel == c, cy = y + 4, rows = 9;
        tx(cx - (c ? 0 : 0), y + 3, c ? band_txt[c - 1] : "PRE", on ? S->hi : S->dim, S->raised, 4);
        int knob = (int)((12 - v) * (rows - 1) / 24 + .5f);
        for (int r = 0; r < rows; r++)
            vx_paint(cx + 1, cy + r, r == knob ? (char)0xDB : (char)0xB3,
                     r == knob ? (on ? S->top : S->bar2) : S->dim, S->raised);
        char n[8];
        int iv = (int)(v < 0 ? -v : v);
        n[0] = v < 0 ? '-' : '+';
        numtxt(n + 1, iv);
        tx(cx, y + 13, iv ? n : " 0", on ? S->hi : S->dim, S->raised, 3);
    }
    tx(x + 2, y + 14, "Preset:", S->dim, S->raised, 8);
    button(x + 10, y + 14, presets[st.preset].n, 0);
    tx(x + 2, y + 16,
       vx_english() ? "Arrows band and gain  T preset  O on/off  R reset"
                    : "Pfeile Band und Pegel  T Preset  O an/aus  R reset",
       S->dim, S->raised, bw - 4);
}
static void draw_help(void) {
    static const char *const de[] = {"PLAYBACK",
                                     "Leertaste  Play / Pause",
                                     "Links/Rechts  5 s zurueck/vor",
                                     "Bild auf/ab  1 Minute",
                                     "Hoch/Runter  Lautstaerke",
                                     "M  Stumm",
                                     "N / P  Naechster / voriger Titel",
                                     "S  Stopp",
                                     "R  Wiederholen",
                                     "Z  Zufall",
                                     "B / V  Balance links / rechts",
                                     "FENSTER",
                                     "E  Equalizer",
                                     "C  Farbschema",
                                     "W  Mini-Player",
                                     "I  Dateiinfo",
                                     "PLAYLIST",
                                     "Tab  Playlist bedienen, Enter spielt",
                                     "A  Dateien und Ordner hinzufuegen",
                                     "Entf  Titel entfernen",
                                     "X  Liste leeren",
                                     "/  Suchen",
                                     0};
    static const char *const en[] = {"PLAYBACK",
                                     "Space  Play / Pause",
                                     "Left/Right  Seek 5 s",
                                     "Page Up/Down  1 minute",
                                     "Up/Down  Volume",
                                     "M  Mute",
                                     "N / P  Next / previous track",
                                     "S  Stop",
                                     "R  Repeat",
                                     "Z  Shuffle",
                                     "B / V  Balance left / right",
                                     "WINDOWS",
                                     "E  Equalizer",
                                     "C  Colour scheme",
                                     "W  Mini player",
                                     "I  File info",
                                     "PLAYLIST",
                                     "Tab  Use the playlist, Enter plays",
                                     "A  Add files and folders",
                                     "Del  Remove track",
                                     "X  Clear list",
                                     "/  Search",
                                     0};
    const char *const *t = vx_english() ? en : de;
    fillr(1, 0, W - 2, H, S->raised);
    tx(3, 0, vx_english() ? "KEYS  (F1, H or Esc closes)" : "TASTEN  (F1, H oder Esc schliesst)",
       S->lcd, S->raised, 40);
    int x = 3, y = 1, half_ = W >= 70 ? 11 : 99, n = 0;
    for (int i = 0; t[i]; i++, n++) {
        if (n == half_) {
            x = W / 2;
            y = 1;
        }
        int head = t[i][0] >= 'A' && t[i][1] >= 'A' && t[i][1] <= 'Z';
        tx(x, y++, t[i], head ? S->lcd : S->text, S->raised, W / 2 - 4);
    }
}
/* Info window like in XZN on other systems, a little slimmer */
static int banner_rows;
static void draw_about(void) {
    int bw = W - 4 < 60 ? W - 4 : 60, bh = 17, x = (W - bw) / 2, y = (H - bh) / 2;
    if (y < 0)
        y = 0;
    fillr(x, y, bw, bh, S->panel);
    fillr(x, y, bw, 1, S->bar1);
    tx(x + 2, y, "Info - XZN", 231, S->bar1, bw - 4);
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 8; i++)
            vx_pixcell(x + 2 + i, y + 2 + j, i, banner_rows + j);
    tx(x + 12, y + 3, "XENON-ZONES (XZN)", S->lcd, S->panel, bw - 14);
    for (int i = 2; i < bw - 2; i++) {
        vx_paint(x + i, y + 7, (char)0xC4, S->dim, S->panel);
        vx_paint(x + i, y + 10, (char)0xC4, S->dim, S->panel);
    }
    tx(x + 2, y + 8, "Classic media player vibes and cyberpunk aesthetics.", S->hi, S->panel,
       bw - 4);
    tx(x + 2, y + 12, "Violyx Version", S->dim, S->panel, bw - 4);
    tx(x + 2, y + 13, "(c) 2026 Merecete", S->dim, S->panel, bw - 4);
    tx(x + 2, y + 14, "Released under the MIT License.", S->dim, S->panel, bw - 4);
    const char *c = vx_english() ? "Close" : "Schliessen";
    button(x + bw - (int)strlen(c) - 5, y + 15, c, 1);
}
static void draw_info(void) {
    int bw = W > 64 ? 60 : W - 4, x = (W - bw) / 2, y = H / 2 - 5;
    char b[40];
    if (y < 0)
        y = 0;
    fillr(x, y, bw, 10, S->raised);
    tx(x + 2, y, "INFO", S->lcd, S->raised, 10);
    if (shown < 0)
        return;
    tx(x + 2, y + 2, title, S->hi, S->raised, bw - 4);
    tx(x + 2, y + 3, list[shown].path, S->text, S->raised, bw - 4);
    tx(x + 2, y + 5,
       fmt == F_MP3    ? (mp3_layer == 2 ? "MP2" : mp3_layer == 1 ? "MP1" : "MP3")
       : fmt == F_OGG  ? "Ogg Vorbis"
       : fmt == F_FLAC ? "FLAC"
       : fmt == F_WAV  ? "WAV"
       : fmt == F_GEN  ? gd.name
                       : "-",
       S->text, S->raised, 12);
    numtxt(b, srate);
    int k = (int)strlen(b);
    memcpy(b + k, " Hz", 4);
    tx(x + 14, y + 5, b, S->text, S->raised, 12);
    tx(x + 26, y + 5, chans > 1 ? "Stereo" : "Mono", S->text, S->raised, 8);
    numtxt(b, kbps);
    k = (int)strlen(b);
    memcpy(b + k, " kbit/s", 8);
    tx(x + 34, y + 5, b, S->text, S->raised, 14);
    tx(x + 2, y + 8, "Esc", S->dim, S->raised, 5);
}
static char find_[32];
static int find_n;
static void find_go(int from) {
    if (!find_n)
        return;
    for (int i = 0; i < nt; i++) {
        int t = (from + i) % nt;
        const char *s = list[t].name;
        for (; *s; s++) {
            int k = 0;
            for (; k < find_n; k++) {
                char a = s[k], b = find_[k];
                if (a >= 'A' && a <= 'Z')
                    a += 32;
                if (b >= 'A' && b <= 'Z')
                    b += 32;
                if (a != b)
                    break;
            }
            if (k == find_n) {
                sel = t;
                return;
            }
        }
    }
}

/* ---------- main screens ---------- */
static long scroll_t;
static void draw_marquee(int x, int y, int w) {
    char b[200];
    int n = 0;
    if (nt && shown >= 0) {
        b[n++] = '[';
        numtxt(b + n, shown + 1);
        n = (int)strlen(b);
        b[n++] = '/';
        numtxt(b + n, nt);
        n = (int)strlen(b);
        b[n++] = ']';
        b[n++] = ' ';
    }
    for (const char *s = errmsg ? errmsg : title; *s && n < 180; s++)
        b[n++] = *s;
    b[n] = 0;
    if (n <= w) {
        fillr(x, y, w, 1, S->bg);
        tx(x, y, b, S->hi, S->bg, w);
        return;
    }
    int o = (int)(scroll_t / 6) % (n + 8);
    for (int i = 0; i < w; i++) {
        int p = (o + i) % (n + 8);
        vx_paint(x + i, y, p < n ? b[p] : ' ', S->hi, S->bg);
    }
}
static void draw_transport(int y, int mini) {
    char t[10], u[10];
    long ms = now_ms(), len = srate ? total / srate : 0;
    mmss(t, ms / 1000);
    mmss(u, len);
    int x = 1;
    fillr(0, y, W, 1, S->panel);
    x += tx(x, y, t, S->lcd, S->bg, 7);
    vx_paint(x++, y, '/', S->dim, S->panel);
    x += tx(x, y, fmt ? u : "--:--", S->lcd, S->bg, 7);
    x += 1;
    x += button(x, y, "|<", 0);
    x += button(x, y, paused || !playing ? ">" : "||", playing && !paused);
    x += button(x, y, "[]", !playing && !fmt);
    x += button(x, y, ">|", 0);
    if (!mini) {
        int r = W - 15;
        static const char *const rp[3] = {"REP:OFF", "REP:ALL", "REP:ONE"};
        r += button(r, y, rp[st.rep], st.rep != 0) + 0;
        button(r, y, vx_english() ? "RND" : "ZUF", st.shuf);
    }
}
static void draw_seek(int y);
/* Mini player like the original: logo, state and the title running as a ticker on top, time and
 * volume slider below, then the buttons and the seek bar. No visualizer. */
static void draw_mini(void) {
    char t[10], u[10];
    fillr(0, 0, W, H, S->panel);
    tx(1, 0, " XZN ", 231, 53, 5);
    vx_paint(7, 0, ' ', S->lcd, S->raised);
    vx_paint(8, 0, !playing ? (char)0xFE : paused ? (char)0xBA : (char)0x10, S->lcd, S->raised);
    vx_paint(9, 0, ' ', S->lcd, S->raised);
    int mx = 11, mw = W - mx - 1;
    const char *s = errmsg ? errmsg : title;
    int n = (int)strlen(s);
    fillr(mx, 0, mw, 1, S->bg);
    if (n) { /* runs from right to left all the time, one copy at a time */
        int per = n + 6 > mw + 2 ? n + 6 : mw + 2, o = (int)(scroll_t / 5) % per;
        for (int i = 0; i < mw; i++) {
            int p = (o + i) % per;
            vx_paint(mx + i, 0, p < n ? s[p] : ' ', S->hi, S->bg);
        }
    }
    long ms = now_ms(), len = srate ? total / srate : 0;
    mmss(t, ms / 1000);
    mmss(u, len);
    fillr(1, 1, 15, 1, S->bg);
    int x = 2;
    x += tx(x, 1, t, S->lcd, S->bg, 7);
    x += tx(x, 1, " / ", S->dim, S->bg, 3);
    tx(x, 1, fmt ? u : "--:--", S->lcd, S->bg, 7);
    int vx = W / 2 - 2, vw = W - vx - 6;
    tx(vx, 1, "VOL", st.mute ? S->top : S->dim, S->panel, 3);
    int k = (st.mute ? 0 : st.vol) * (vw - 1) / 100;
    for (int i = 0; i < vw; i++)
        vx_paint(vx + 4 + i, 1, (char)(i == k ? 0xDB : 0xC4),
                 i == k  ? S->hi
                 : i < k ? S->bar2
                         : S->raised,
                 S->panel);
    x = 1;
    x += button(x, 2, "|<", 0);
    x += button(x, 2, paused || !playing ? ">" : "||", playing && !paused);
    x += button(x, 2, "[]", !playing && !fmt);
    button(x, 2, ">|", 0);
    static const char *const rp[3] = {"REP:OFF", "REP:ALL", "REP:ONE"};
    const char *z = vx_english() ? "RND" : "ZUF";
    int r = W - 1 - ((int)strlen(rp[st.rep]) + 3) - ((int)strlen(z) + 3);
    r += button(r, 2, rp[st.rep], st.rep != 0);
    button(r, 2, z, st.shuf);
    draw_seek(3);
}
static void draw_seek(int y) {
    long ms = now_ms(), len = srate ? total * 1000 / srate : 0;
    int p = len ? (int)(ms * W / len) : 0;
    for (int i = 0; i < W; i++)
        vx_paint(i, y,
                 (char)(i == p  ? 0xDB
                        : i < p ? 0xCD
                                : 0xC4),
                 i <= p ? S->bar2 : S->raised, S->panel);
}
static void draw_vol(int y) {
    char b[8];
    fillr(0, y, W, 1, S->panel);
    tx(1, y, "VOL", st.mute ? S->top : S->dim, S->panel, 3);
    slider(5, y, 20, st.mute ? 0 : st.vol, 100, S->bar2);
    numtxt(b, st.vol);
    tx(26, y, b, S->dim, S->panel, 3);
    tx(31, y, "BAL", S->dim, S->panel, 3);
    for (int i = 0; i < 16; i++)
        vx_paint(35 + i, y, (char)(i == (st.bal + 100) * 15 / 200 ? 0xDB : 0xC4),
                 i == (st.bal + 100) * 15 / 200 ? S->bar2 : S->raised, S->panel);
    tx(W - 18, y, "EQ", st.eqon ? S->lcd : S->dim, S->panel, 2);
    tx(W - 15, y, presets[st.preset].n, S->dim, S->panel, 9);
    tx(W - 5, y, st.mute ? "MUTE" : "", S->top, S->panel, 4);
}
static void draw_list(int y, int rows) {
    char b[10];
    fillr(0, y, W, 1, S->raised);
    tx(1, y, "PLAYLIST", focus_list ? S->hi : S->lcd, S->raised, 10);
    numtxt(b, nt);
    int k = (int)strlen(b);
    tx(W - k - 7, y, b, S->dim, S->raised, k);
    tx(W - 6, y, vx_english() ? "items" : "Titel", S->dim, S->raised, 5);
    if (focus_list) {
        if (sel < top_)
            top_ = sel;
        if (sel >= top_ + rows)
            top_ = sel - rows + 1;
    } else if (shown >= 0 && !(shown >= top_ && shown < top_ + rows)) {
        top_ = shown - rows / 2;
    }
    if (top_ > nt - rows)
        top_ = nt - rows;
    if (top_ < 0)
        top_ = 0;
    for (int i = 0; i < rows; i++) {
        int t = top_ + i, r = y + 1 + i, s = focus_list && t == sel;
        uint8_t bg = s ? S->raised : S->bg;
        fillr(0, r, W, 1, bg);
        if (t >= nt)
            continue;
        int me = t == shown;
        if (me)
            vx_paint(0, r, (char)0x0E, S->top, bg);
        tx(2, r, list[t].name, me ? S->top : S->text, bg, W - 10);
        if (list[t].secs) {
            mmss(b, list[t].secs);
            tx(W - 6, r, b, S->dim, bg, 6);
        }
    }
}
static void draw(long t) {
    W = vx_cols();
    H = vx_rows();
    vx_clear(0);
    fillr(0, 0, W, H, S->panel);
    if (st.mini) {
        draw_mini();
        vx_show();
        return;
    }
    int bh = H >= 30 ? 4 : 2;
    banner_rows = bh; /* small banner, twice as high on big screens like the original */
    int spec = 3, y = 0;
    int rest = H - (bh + 5 + spec + 2);
    if (rest > 14) {
        spec = 5;
        rest -= 2;
    }
    banner(0, bh, t);
    y = bh;
    fillr(0, y, W, 1, S->bg);
    vx_paint(1, y, !playing ? (char)0xFE : paused ? (char)0xBA : (char)0x10, S->lcd, S->bg);
    draw_marquee(3, y, W - 4);
    y++;
    draw_transport(y, 0);
    y++;
    draw_seek(y);
    y++;
    draw_vol(y);
    y++;
    spectrum(y, spec, W, W >= 120 ? 60 : W / 2);
    y += spec;
    draw_list(y, H - y - 2);
    y = H - 1;
    fillr(0, y, W, 1, S->raised);
    if (overlay == O_FIND) {
        tx(1, y, vx_english() ? "Search: " : "Suche: ", S->lcd, S->raised, 8);
        tx(9, y, find_, S->hi, S->raised, 30);
    } else {
        char b[80];
        int n = 0;
        if (fmt) {
            const char *f = fmt == F_MP3    ? (mp3_layer == 2 ? "mp2" : mp3_layer == 1 ? "mp1" : "mp3")
                            : fmt == F_OGG  ? "ogg vorbis"
                            : fmt == F_FLAC ? "flac"
                            : fmt == F_GEN  ? gd.name
                                            : "wav";
            for (; *f && n < 30; f++)
                b[n++] = *f >= 'A' && *f <= 'Z' ? (char)(*f + 32) : *f;
            b[n++] = ' ';
            numtxt(b + n, srate);
            n = (int)strlen(b);
            b[n++] = ' ';
            b[n++] = 'H';
            b[n++] = 'z';
            b[n++] = ' ';
            const char *ch_ = chans > 1 ? "stereo" : "mono";
            for (; *ch_; ch_++)
                b[n++] = *ch_;
        }
        b[n] = 0;
        tx(1, y, b, S->dim, S->raised, W / 2);
        const char *h =
            vx_english() ? "F1 keys  A add  Tab list  E EQ" : "F1 Tasten  A hinzu  Tab Liste  E EQ";
        tx(W - (int)strlen(h) - 1, y, h, S->dim, S->raised, W);
    }
    if (overlay == O_EQ)
        draw_eq();
    if (overlay == O_HELP)
        draw_help();
    if (overlay == O_ABOUT)
        draw_about();
    if (overlay == O_INFO)
        draw_info();
    if (overlay == O_ADD)
        draw_picker();
    vx_show();
}
static void set_overlay(int o) {
    overlay = o;
    vx_sys(23, 3, o != O_NONE, 0);
}
static void toggle_pause(void) {
    if (!playing && !fmt) {
        if (nt) {
            long r = resume_ms;
            play(cur >= 0 ? cur : 0, r, 0);
        }
        return;
    }
    if (!playing) {
        play(cur, resume_ms, 0);
        return;
    }
    paused = !paused;
    au_pause(paused);
}
static void set_mini(int on) {
    st.mini = on;
    if (on)
        vx_sys(23, 4, 50, 4);
    else
        vx_sys(23, 5, 0, 0);
    save_state();
}
static void set_overlay(int o);
static void set_mini(int on);
static void key(int full) {
    int k = full & 0xFFFF, sh = (full >> 16) & 1;
    if (k >= VK_MENU && k < VK_MENU + 4) { /* the XZN menu in the menu bar */
        if (k == VK_MENU + 3) {
            set_overlay(O_NONE);
            set_mini(!st.mini);
            return;
        }
        if (k == VK_MENU + 2) {
            st.scheme = (st.scheme + 1) % 5;
            S = &schemes[st.scheme];
            save_state();
            return;
        }
        if (st.mini)
            set_mini(0);
        set_overlay(k == VK_MENU ? O_ABOUT : O_HELP);
        return;
    }
    if (overlay == O_FIND) {
        if (k == VK_ESC || k == VK_ENTER) {
            set_overlay(O_NONE);
            if (k == VK_ENTER) {
                play(sel, 0, 0);
            }
            return;
        }
        if (k == VK_BS && find_n)
            find_[--find_n] = 0;
        else if (k >= 32 && k < 127 && find_n < 30) {
            find_[find_n++] = (char)k;
            find_[find_n] = 0;
        } else if (k == VK_DOWN) {
            find_go(sel + 1);
            return;
        }
        find_go(0);
        return;
    }
    if (overlay == O_EQ) {
        if (k == VK_ESC || k == 'e' || k == 'E')
            set_overlay(O_NONE);
        else
            eq_key(k);
        return;
    }
    if (overlay == O_ADD) {
        if (k == VK_ESC)
            set_overlay(O_NONE);
        else
            pk_key(k);
        return;
    }
    if (overlay != O_NONE) {
        set_overlay(O_NONE);
        return;
    }
    if (focus_list) {
        int page = H > 24 ? H - 16 : 8;
        switch (k) {
            case VK_UP:
                if (sel)
                    sel--;
                return;
            case VK_DOWN:
                if (sel < nt - 1)
                    sel++;
                return;
            case VK_PGUP:
                sel -= page;
                if (sel < 0)
                    sel = 0;
                return;
            case VK_PGDN:
                sel += page;
                if (sel >= nt)
                    sel = nt - 1;
                if (sel < 0)
                    sel = 0;
                return;
            case VK_HOME:
                sel = 0;
                return;
            case VK_END:
                sel = nt ? nt - 1 : 0;
                return;
            case VK_ENTER:
                if (nt)
                    play(sel, 0, 0);
                return;
            case VK_DEL:
                if (nt) {
                    if (sel == cur)
                        stop_();
                    memmove(&list[sel], &list[sel + 1], sizeof(track) * (size_t)(nt - sel - 1));
                    nt--;
                    if (cur > sel) {
                        cur--;
                        shown--;
                    } else if (cur == sel) {
                        cur = shown = -1;
                        title[0] = 0;
                    }
                    if (sel >= nt && sel)
                        sel--;
                    save_list();
                }
                return;
        }
    }
    switch (k) {
        case ' ':
            toggle_pause();
            break;
        case VK_LEFT:
            seek_to(now_ms() - (sh ? 30000 : 5000));
            break;
        case VK_RIGHT:
            seek_to(now_ms() + (sh ? 30000 : 5000));
            break;
        case VK_PGUP:
            seek_to(now_ms() - 60000);
            break;
        case VK_PGDN:
            seek_to(now_ms() + 60000);
            break;
        case VK_UP:
            st.vol += 5;
            if (st.vol > 100)
                st.vol = 100;
            st.mute = 0;
            set_gain();
            break;
        case VK_DOWN:
            st.vol -= 5;
            if (st.vol < 0)
                st.vol = 0;
            set_gain();
            break;
        case 'm':
        case 'M':
            st.mute = !st.mute;
            set_gain();
            break;
        case 'n':
        case 'N':
            next_track(1, 0);
            break;
        case 'p':
        case 'P':
            if (now_ms() > 3000)
                seek_to(0);
            else
                next_track(-1, 0);
            break;
        case 's':
        case 'S':
            stop_();
            break;
        case 'r':
        case 'R':
            st.rep = (st.rep + 1) % 3;
            save_state();
            break;
        case 'z':
        case 'Z':
            st.shuf = !st.shuf;
            save_state();
            break;
        case 'b':
        case 'B':
            st.bal -= 10;
            if (st.bal < -100)
                st.bal = -100;
            set_gain();
            break;
        case 'v':
        case 'V':
            st.bal += 10;
            if (st.bal > 100)
                st.bal = 100;
            set_gain();
            break;
        case 'c':
        case 'C':
            st.scheme = (st.scheme + 1) % 5;
            S = &schemes[st.scheme];
            save_state();
            break;
        case 'e':
        case 'E':
            set_overlay(O_EQ);
            break;
        case 'h':
        case 'H':
        case VK_F1:
            set_overlay(O_HELP);
            break;
        case 'i':
        case 'I':
            set_overlay(O_INFO);
            break;
        case 'a':
        case 'A':
            pk_load();
            set_overlay(O_ADD);
            break;
        case 'w':
        case 'W':
            set_mini(!st.mini);
            break;
        case 'x':
        case 'X':
            stop_();
            nt = 0;
            cur = shown = -1;
            sel = 0;
            title[0] = 0;
            save_list();
            break;
        case '/':
            find_n = 0;
            find_[0] = 0;
            focus_list = 1;
            set_overlay(O_FIND);
            break;
        case VK_TAB:
            focus_list = !focus_list;
            if (focus_list && shown >= 0)
                sel = shown;
            break;
    }
}
int main(void) {
    S = &schemes[0];
    saved s;
    long n = vx_read("state", &s, sizeof s);
    if (n == sizeof s) {
        st = s;
        if (st.vol < 0 || st.vol > 100)
            st.vol = 80;
        if (st.bal < -100 || st.bal > 100)
            st.bal = 0;
        if (st.rep < 0 || st.rep > 2)
            st.rep = 0;
        if (st.scheme < 0 || st.scheme > 4)
            st.scheme = 0;
        if (st.preset < 0 || st.preset > 7)
            st.preset = 0;
        for (int i = 0; i < 10; i++)
            if (!(st.eq[i] >= -12 && st.eq[i] <= 12))
                st.eq[i] = 0;
        if (!(st.pre >= -12 && st.pre <= 12))
            st.pre = 0;
        S = &schemes[st.scheme];
    }
    load_list();
    vx_sys(23, 2, 0, 0); /* keep running while other windows are on top */
    vx_sys(23, 8, 1, 0); /* F1 shows the XZN keys, like in the original */
    vx_menu(vx_english() ? "Info|Keys (F1)|Colour scheme (C)|Mini player (W)"
                         : "Info|Tasten (F1)|Farbschema (C)|Mini-Player (W)");
    vx_sys(23, 5, 0, 0);
    if (st.mini)
        vx_sys(23, 4, 50, 4);
    eq_update();
    set_gain();
    if (au_free() < 0)
        errmsg = vx_english() ? "No sound card found" : "Keine Soundkarte gefunden";
    else if (nt) { /* the last track waits at its position: Space starts it */
        cur = st.cur >= 0 && st.cur < nt ? st.cur : 0;
        resume_ms = (long)st.pos * 1000;
        set_title(cur);
    }
    long next = 0, savet = vx_ms() + 20000, t0 = vx_ms();
    for (;;) {
        long full;
        while ((full = vx_sys(3, 0, 0, 0)) >= 0)
            key((int)full);
        pump();
        long now = vx_ms();
        if (now >= next) {
            next = now + 66;
            scroll_t++;
            draw(now - t0);
        }
        if (now >= savet) {
            savet = now + 20000;
            if (playing)
                save_state();
        }
        vx_wait(playing ? 20 : 60);
    }
}
#ifdef HOSTTEST
long xt_run(const char *path, long *sum) {
    static short o[BLOCK * 2];
    nt = 0;
    add_track(path, "x");
    eq_preset(1);
    st.pre = -3;
    eq_update();
    if (!load_track(0))
        return -1;
    reset_path();
    long frames = 0;
    unsigned long h = 5381;
    for (;;) {
        int n = produce(o, BLOCK);
        if (n <= 0)
            break;
        shape(o, n);
        for (int i = 0; i < n * 2; i += 97)
            h = h * 33 + (unsigned short)o[i];
        frames += n;
        if (frames > 48000L * 2000)
            break;
    }
    if (fmt)
        seek_src(total / 2);
    short b[64];
    read_src(b, 16);
    *sum = (long)h;
    return frames;
}
#endif
