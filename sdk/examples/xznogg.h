/* OGG Vorbis for XZN's CD rips: 16-bit stereo at 44.1 kHz, quality 6 (about 190 kbit/s), with
   the Vorbis encoder of Xiph.org (libvorbis and libogg, BSD-3-Clause, in third_party).
   SPDX-License-Identifier: BSD-3-Clause */
#include <vorbis/vorbisenc.h>
#define XO_QUALITY 0.6f
typedef struct {
    ogg_stream_state os;
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    int on;
} xo_enc;
/* the finished pages to o; returns the bytes written */
static long xo_pages(xo_enc *e, uint8_t *o, int flush) {
    ogg_page pg;
    long n = 0;
    while (flush ? ogg_stream_flush(&e->os, &pg) : ogg_stream_pageout(&e->os, &pg)) {
        memcpy(o + n, pg.header, (size_t)pg.header_len);
        n += pg.header_len;
        memcpy(o + n, pg.body, (size_t)pg.body_len);
        n += pg.body_len;
    }
    return n;
}
static long xo_drain(xo_enc *e, uint8_t *o) {
    ogg_packet p;
    long n = 0;
    while (vorbis_analysis_blockout(&e->vd, &e->vb) == 1) {
        vorbis_analysis(&e->vb, 0);
        vorbis_bitrate_addblock(&e->vb);
        while (vorbis_bitrate_flushpacket(&e->vd, &p)) {
            ogg_stream_packetin(&e->os, &p);
            n += xo_pages(e, o + n, 0);
        }
    }
    return n;
}
static void xo_end(xo_enc *e) {
    if (!e->on)
        return;
    ogg_stream_clear(&e->os);
    vorbis_block_clear(&e->vb);
    vorbis_dsp_clear(&e->vd);
    vorbis_comment_clear(&e->vc);
    vorbis_info_clear(&e->vi);
    e->on = 0;
}
/* The three headers with the tags (key=value pairs, CP437) to o (needs 64 KB).
   Returns their length or -1. */
static long xo_begin(xo_enc *e, uint32_t serial, const char *const *tags, int ntags, uint8_t *o) {
    xo_end(e);
    vorbis_info_init(&e->vi);
    if (vorbis_encode_init_vbr(&e->vi, 2, 44100, XO_QUALITY)) {
        vorbis_info_clear(&e->vi);
        return -1;
    }
    e->on = 1;
    vorbis_comment_init(&e->vc);
    for (int i = 0; i < ntags; i++) {
        const char *eq = tags[i];
        while (*eq && *eq != '=')
            eq++;
        if (!*eq || !eq[1])
            continue;
        char key[24];
        int k = (int)(eq - tags[i]);
        if (k > 23)
            k = 23;
        memcpy(key, tags[i], (size_t)k);
        key[k] = 0;
        uint8_t val[400];
        int l = xf_utf8(eq + 1, val, 399);
        val[l] = 0;
        vorbis_comment_add_tag(&e->vc, key, (char *)val);
    }
    vorbis_analysis_init(&e->vd, &e->vi);
    vorbis_block_init(&e->vd, &e->vb);
    ogg_stream_init(&e->os, (int)serial);
    ogg_packet h, hc, hb;
    vorbis_analysis_headerout(&e->vd, &e->vc, &h, &hc, &hb);
    ogg_stream_packetin(&e->os, &h);
    ogg_stream_packetin(&e->os, &hc);
    ogg_stream_packetin(&e->os, &hb);
    return xo_pages(e, o, 1); /* the sound starts on a page of its own */
}
/* n stereo samples (n = 0: the end of the track). Returns the bytes written to o; o needs
   room for about n bytes plus 64 KB. */
static long xo_feed(xo_enc *e, const int16_t *pcm, int n, uint8_t *o) {
    if (!e->on)
        return 0;
    if (n > 0) {
        float **b = vorbis_analysis_buffer(&e->vd, n);
        for (int i = 0; i < n; i++) {
            b[0][i] = pcm[2 * i] * (1.0f / 32768);
            b[1][i] = pcm[2 * i + 1] * (1.0f / 32768);
        }
    }
    vorbis_analysis_wrote(&e->vd, n > 0 ? n : 0);
    long k = xo_drain(e, o);
    if (n <= 0)
        k += xo_pages(e, o + k, 1);
    return k;
}
