/* Voice notes: records the microphone into a WAV file in /home/Recordings, lists the notes and
 * plays them. A program in ring 3; the kernel gives it the microphone (rec_start) and shows REC
 * in the status line while it records.
 *
 * Recording: the frames go into a raw file next to the notes, mono 16 bit. Many audio interfaces
 * have the microphone on one channel only, so the first seconds decide: one quiet channel and
 * the other one is taken alone, otherwise both are mixed. On stop the WAV file is made from the
 * raw file (a header and the data in parts of 1 MB) and the raw file goes away. */
#include "common.h"
#define DIR  "/home/Recordings"
#define RAWP DIR "/.recording-" /* + rate + ".raw": kept when the computer stops mid-way */
#define CFG  "/system/voice" /* the chosen input */
#define MAXN 200
#define PREN (48000 * 4) /* stereo frames looked at before deciding the channel */
#define OUTN 48000       /* mono samples written at a time */
typedef struct {
    char name[64];
    u64 size;
    u32 secs;
} note_t;
typedef struct {
    note_t *n;
    int nn, sel, top;
    /* recording */
    int rec, rate, mode; /* mode: 0 not yet decided, 1 left, 2 right, 3 both mixed */
    char dev[44], file[96], raw[96];
    short *in, *pre, *out;
    int pren, outn, peak, peakl, peakr, failed;
    u64 frames, el, er;
    u64 start;
    /* playing */
    int play, prate, pch;
    u8 *pd;
    u64 plen, ppos; /* bytes of sound data, position in frames of the file */
    u32 pacc;
    short *pbuf;
    char msg[90];
    u64 msgt;
    int input, ninputs; /* the chosen input, how many there are */
    char inname[40];
} vn_t;
/* the inputs: name of the chosen one, or the first when it is gone */
static void inputs(vn_t *v) {
    char t[40];
    v->ninputs = 0;
    while (v->ninputs < 16 && rec_input(v->ninputs, t))
        v->ninputs++;
    if (v->input >= v->ninputs)
        v->input = 0;
    if (!v->ninputs || !rec_input(v->input, v->inname))
        v->inname[0] = 0;
}
static void say(vn_t *v, const char *m) {
    strlcpy_(v->msg, m, sizeof v->msg);
    v->msgt = ticks + 400;
}
static void put32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}
static u32 get32(const u8 *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}
static void wav_head(u8 *h, u32 rate, u32 bytes) {
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put32(h + 20, 1 | 1 << 16); /* PCM, mono */
    put32(h + 24, rate);
    put32(h + 28, rate * 2);
    put32(h + 32, 2 | 16 << 16); /* 2 bytes a frame, 16 bit */
    memcpy(h + 36, "data", 4);
    put32(h + 40, bytes);
}
/* ---------- the list ---------- */
static int cmpn(const note_t *a, const note_t *b) {
    return strcmp(a->name, b->name);
}
static int add_note(const vfs_ent *e, void *a) {
    vn_t *v = a;
    int l = (int)strlen(e->name);
    if (e->dir || l < 5 || l > 63 || e->name[0] == '.' || v->nn >= MAXN)
        return 0;
    const char *x = e->name + l - 4;
    if (!(x[0] == '.' && (x[1] | 32) == 'w' && (x[2] | 32) == 'a' && (x[3] | 32) == 'v'))
        return 0;
    note_t *n = &v->n[v->nn++];
    strlcpy_(n->name, e->name, sizeof n->name);
    n->size = e->size;
    n->secs = 0;
    char p[VFS_PATH];
    ksnprintf(p, sizeof p, "%s/%s", DIR, e->name);
    u8 h[44];
    if (fsx_read_at(p, h, 0, 44) == 44 && !memcmp(h, "RIFF", 4)) {
        u32 bps = get32(h + 28);
        if (bps)
            n->secs = (u32)((e->size > 44 ? e->size - 44 : 0) / bps);
    }
    return 0;
}
static void load_list(vn_t *v, const char *select) {
    v->nn = 0;
    fsx_list(DIR, add_note, v);
    for (int i = 1; i < v->nn; i++) /* newest first: the names carry date and time */
        for (int j = i; j > 0 && cmpn(&v->n[j - 1], &v->n[j]) < 0; j--) {
            note_t t = v->n[j];
            v->n[j] = v->n[j - 1];
            v->n[j - 1] = t;
        }
    if (select)
        for (int i = 0; i < v->nn; i++)
            if (!strcmp(v->n[i].name, select))
                v->sel = i;
    if (v->sel >= v->nn)
        v->sel = v->nn ? v->nn - 1 : 0;
}
/* ---------- recording ---------- */
static void flush_out(vn_t *v) {
    if (!v->outn || v->failed)
        return;
    long r = fsx_append(v->raw, v->out, (u64)v->outn * 2);
    if (r < 0) {
        v->failed = 1;
        say(v, r == VE_NOSPC ? TR("Kein Platz mehr: Aufnahme beendet", "No room left: recording stopped")
                             : TR("Schreiben fehlgeschlagen", "Writing failed"));
    }
    v->outn = 0;
}
static void take(vn_t *v, short l, short r) {
    int s = v->mode == 1 ? l : v->mode == 2 ? r : (l + r) / 2;
    v->out[v->outn++] = (short)s;
    if (v->outn == OUTN)
        flush_out(v);
}
static void decide(vn_t *v, int force) {
    /* one channel much quieter than the other: the microphone is on the loud one */
    u64 lo = v->el < v->er ? v->el : v->er, hi = v->el < v->er ? v->er : v->el;
    if (!force && hi < (u64)v->pren * 350 && v->pren < PREN)
        return; /* still silence: wait a little longer */
    v->mode = lo * 30 < hi ? (v->el > v->er ? 1 : 2) : 3;
    for (int i = 0; i < v->pren; i++)
        take(v, v->pre[2 * i], v->pre[2 * i + 1]);
    v->pren = 0;
}
static void rec_poll(vn_t *v) {
    if (!v->rec)
        return;
    for (int round = 0; round < 8; round++) {
        long n = rec_read(v->in, 9600);
        if (n < 0) {
            say(v, TR("Das Mikrofon ist weg", "The microphone is gone"));
            v->failed = 1;
            break;
        }
        if (n == 0)
            break;
        int pk = 0, pl = 0, pr = 0;
        for (long i = 0; i < n; i++) {
            int l = v->in[2 * i], r = v->in[2 * i + 1];
            int al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            if (al > pl)
                pl = al;
            if (ar > pr)
                pr = ar;
            if (!v->mode) {
                v->el += (u64)(al * al) >> 8;
                v->er += (u64)(ar * ar) >> 8;
                v->pre[2 * v->pren] = (short)l;
                v->pre[2 * v->pren + 1] = (short)r;
                if (++v->pren == PREN)
                    decide(v, 1);
            } else
                take(v, (short)l, (short)r);
        }
        if (!v->mode && v->pren >= 48000 / 2)
            decide(v, 0);
        v->frames += (u64)n;
        pk = v->mode == 1 ? pl : v->mode == 2 ? pr : pl > pr ? pl : pr;
        v->peak = pk > v->peak ? pk : v->peak;
        v->peakl = pl;
        v->peakr = pr;
    }
}
/* The WAV file from a raw file: the header, then the data in parts of 1 MB */
static void make_wav(vn_t *v, const char *raw, int rate, const char *file) {
    vfs_ent e;
    if (fsx_stat(raw, &e) || !e.size) {
        fsx_remove(raw);
        say(v, TR("Nichts aufgenommen", "Nothing recorded"));
        return;
    }
    u8 *buf = kmalloc(1 << 20);
    if (!buf) {
        say(v, TR("Zu wenig Speicher", "Not enough memory"));
        return;
    }
    char path[VFS_PATH];
    ksnprintf(path, sizeof path, "%s/%s", DIR, file);
    wav_head(buf, (u32)rate, (u32)e.size);
    long r = fsx_write(path, buf, 44, fs_now());
    for (u64 o = 0; !r && o < e.size;) {
        u64 k = e.size - o < (1 << 20) ? e.size - o : (1 << 20);
        if (fsx_read_at(raw, buf, o, k) != (int64_t)k)
            r = VE_IO;
        else
            r = fsx_append(path, buf, k);
        o += k;
    }
    kfree(buf);
    if (r < 0) {
        fsx_remove(path); /* keep the raw data, maybe the next try has room */
        say(v, r == VE_NOSPC ? TR("Kein Platz fuer die Datei", "No room for the file")
                             : TR("Speichern fehlgeschlagen", "Saving failed"));
        return;
    }
    fsx_remove(raw);
    char m[90];
    ksnprintf(m, sizeof m, "%s %s", TR("Gespeichert:", "Saved:"), file);
    say(v, m);
    load_list(v, file);
}
static void stop_rec(vn_t *v) {
    if (!v->rec)
        return;
    rec_poll(v);
    rec_stop();
    v->rec = 0;
    ui_bgframes(v->play);
    if (!v->mode)
        decide(v, 1);
    flush_out(v);
    if (v->failed && v->frames < (u64)v->rate / 4) {
        fsx_remove(v->raw);
        return;
    }
    make_wav(v, v->raw, v->rate, v->file);
}
static void stop_play(vn_t *v);
static void rescue(vn_t *v);
static void note_name(char *o, int n, const char *what) {
    rtc_t t;
    rtc_read(&t);
    ksnprintf(o, n, "%s %d-%02d-%02d %02d-%02d-%02d.wav", what, t.year, t.mon, t.day, t.h, t.m, t.s);
}
static void start_rec(vn_t *v) {
    stop_play(v);
    fsx_mkdir(DIR, fs_now());
    long fr = fsx_free(DIR);
    if (fr >= 0 && fr < (2 << 20)) {
        say(v, TR("Kein Platz mehr in /home", "No room left in /home"));
        return;
    }
    rescue(v); /* raw data kept from a recording that could not be saved: a note first */
    rec_info ri;
    long r = rec_start(&ri, v->input);
    if (r < 0) {
        say(v, r == -12  ? TR("Ein anderes Programm nimmt gerade auf", "Another program is recording")
               : r == -4 ? TR("Das Mikrofon startet nicht, Befehl sound zeigt warum",
                              "The microphone does not start, the sound command shows why")
                         : TR("Kein Mikrofon gefunden", "No microphone found"));
        return;
    }
    ksnprintf(v->raw, sizeof v->raw, "%s%d.raw", RAWP, ri.rate);
    if (fsx_write(v->raw, "", 0, fs_now()) < 0) {
        rec_stop();
        say(v, TR("Kann in /home nicht schreiben", "Cannot write to /home"));
        return;
    }
    note_name(v->file, sizeof v->file, TR("Notiz", "Note"));
    strlcpy_(v->dev, ri.name, sizeof v->dev);
    v->rate = ri.rate;
    v->rec = 1;
    v->mode = 0;
    v->pren = v->outn = 0;
    v->el = v->er = 0;
    v->frames = 0;
    v->failed = 0;
    v->peak = 0;
    v->start = ticks;
    ui_bgframes(1);
}
/* ---------- playing ---------- */
static void stop_play(vn_t *v) {
    if (!v->play)
        return;
    v->play = 0;
    snd_stream_flush();
    kfree(v->pd);
    v->pd = 0;
    ui_bgframes(v->rec);
}
static void start_play(vn_t *v) {
    if (!v->nn || v->rec)
        return;
    stop_play(v);
    note_t *n = &v->n[v->sel];
    if (n->size > (64u << 20)) {
        say(v, TR("Zu lang zum Abspielen", "Too long to play"));
        return;
    }
    char p[VFS_PATH];
    ksnprintf(p, sizeof p, "%s/%s", DIR, n->name);
    u8 *d = kmalloc(n->size + 1);
    if (!d || fsx_read(p, d, n->size) != (int64_t)n->size) {
        kfree(d);
        say(v, TR("Kann die Datei nicht lesen", "Cannot read the file"));
        return;
    }
    /* RIFF: fmt and data, 16-bit PCM with 1 or 2 channels */
    u64 o = 12, dat = 0, dl = 0;
    int ch = 0, rate = 0, bits = 0;
    while (o + 8 <= n->size && memcmp(d, "RIFF", 4) == 0) {
        u32 l = get32(d + o + 4);
        if (!memcmp(d + o, "fmt ", 4) && l >= 16 && o + 24 <= n->size) {
            ch = d[o + 10];
            rate = (int)get32(d + o + 12);
            bits = d[o + 22];
            if ((d[o + 8] | d[o + 9] << 8) != 1)
                bits = 0;
        } else if (!memcmp(d + o, "data", 4)) {
            dat = o + 8;
            dl = l < n->size - dat ? l : n->size - dat;
            break;
        }
        o += 8 + l + (l & 1);
    }
    if (!dat || bits != 16 || ch < 1 || ch > 2 || rate < 8000 || rate > 192000) {
        kfree(d);
        say(v, TR("Kein unterstuetztes WAV", "Not a supported WAV"));
        return;
    }
    memmove(d, d + dat, dl);
    v->pd = d;
    v->plen = dl;
    v->pch = ch;
    v->prate = rate;
    v->ppos = 0;
    v->pacc = 0;
    v->play = 1;
    ui_bgframes(1);
}
static void play_poll(vn_t *v) {
    if (!v->play)
        return;
    const short *s = (const short *)v->pd;
    u64 total = v->plen / 2 / (u64)v->pch;
    for (;;) {
        long room = snd_stream(0, 0);
        if (room < 0) {
            say(v, TR("Keine Tonausgabe", "No sound output"));
            stop_play(v);
            return;
        }
        if (room < 2400 || v->ppos >= total)
            break;
        int n = 0;
        u32 step = (u32)(((u64)v->prate << 16) / 48000); /* 48 kHz out, any rate in */
        while (n < 4800 && v->ppos < total) {
            u64 i = v->ppos;
            short l = s[i * (u64)v->pch], r = v->pch == 2 ? s[i * 2 + 1] : l;
            v->pbuf[2 * n] = l;
            v->pbuf[2 * n + 1] = r;
            n++;
            v->pacc += step;
            v->ppos += v->pacc >> 16;
            v->pacc &= 0xFFFF;
        }
        snd_stream(v->pbuf, n);
    }
    if (v->ppos >= total && snd_stream(0, 0) >= 131072 - 2400) {
        v->play = 0; /* everything was played */
        kfree(v->pd);
        v->pd = 0;
        ui_bgframes(v->rec);
    }
}
/* ---------- the window ---------- */
static void tm(char *o, int n, u32 s) {
    ksnprintf(o, n, "%02d:%02d", s / 60, s % 60);
}
static void vn_draw(win_t *w) {
    vn_t *v = w->st;
    cfill(w, T);
    int cols = cw(w), rows_ = ch(w);
    char l[120], a[16], b[16];
    /* line 1: what happens */
    if (v->rec) {
        tm(a, 16, (u32)(v->frames / (u64)(v->rate ? v->rate : 48000)));
        ksnprintf(l, sizeof l, "%s  %s", TR("Aufnahme", "Recording"), a);
        ctext(w, 1, 0, AT(C_PANEL, C_HOT), "\x07");
        ctext(w, 3, 0, HI, l);
        ksnprintf(l, sizeof l, "%s", v->dev);
        ctext(w, cols - (int)strlen(l) - 1 > 18 ? cols - (int)strlen(l) - 1 : 18, 0, DIM, l);
    } else if (v->play) {
        u64 total = v->plen / 2 / (u64)v->pch;
        tm(a, 16, (u32)(v->ppos / (u64)v->prate));
        tm(b, 16, (u32)(total / (u64)v->prate));
        ksnprintf(l, sizeof l, "%s  %s / %s", TR("Wiedergabe", "Playing"), a, b);
        ctext(w, 1, 0, AT(C_PANEL, C_NEON), "\x10");
        ctext(w, 3, 0, HI, l);
    } else if (v->ninputs) {
        ksnprintf(l, sizeof l, "%s %s", TR("Eingang:", "Input:"), v->inname);
        ctext(w, 1, 0, T, l);
        if (v->ninputs > 1)
            ctext(w, cols - 11, 0, DIM, TR("M wechselt", "M switches"));
    } else
        ctext(w, 1, 0, AT(C_PANEL, C_GLOW),
              TR("Kein Mikrofon gefunden (Befehl sound zeigt warum)",
                 "No microphone found (the sound command shows why)"));
    /* line 2: the level, in steps of 3 dB */
    int bw = cols - 10;
    if (bw > 4) {
        ctext(w, 1, 1, DIM, TR("Pegel", "Level"));
        int lv = 0;
        if (v->rec && v->peak > 0) {
            int p = v->peak, db = 0; /* -dB below full scale */
            while (p < 23170 && db < 60) {
                p = p * 1414 / 1000;
                db += 3;
            }
            lv = (60 - db) * bw / 60;
        }
        for (int x = 0; x < bw; x++) {
            int on = x < lv;
            u8 c = x >= bw * 9 / 10 ? C_HOT : x >= bw * 7 / 10 ? C_GLOW : C_NEON;
            cput(w, 8 + x, 1, on ? 0xDB : 0xFA, AT(C_PANEL, on ? c : C_DIM));
        }
        v->peak = v->peak * 2 / 3; /* falls back slowly */
    }
    /* the notes */
    int top = 3, rows = rows_ - top - 1;
    if (v->msg[0] && ticks < v->msgt)
        ctext(w, 1, 2, AT(C_PANEL, C_GLOW), v->msg);
    if (!v->nn)
        ctext(w, 1, top, DIM, TR("Noch keine Notizen in /home/Recordings", "No notes yet in /home/Recordings"));
    if (v->sel < v->top)
        v->top = v->sel;
    if (rows > 0 && v->sel >= v->top + rows)
        v->top = v->sel - rows + 1;
    for (int i = 0; i < rows && v->top + i < v->nn; i++) {
        note_t *n = &v->n[v->top + i];
        int s = v->top + i == v->sel;
        char nm[64];
        strlcpy_(nm, n->name, sizeof nm);
        nm[strlen(nm) - 4] = 0;
        tm(a, 16, n->secs);
        sizes(n->size, b, 16);
        u8 at = s ? SEL : T;
        for (int x = 0; x < cols; x++)
            cput(w, x, top + i, ' ', at);
        ctext(w, 1, top + i, at, nm);
        ksnprintf(l, sizeof l, "%s  %s", a, b);
        while (strlen(l) < 15) { /* the sizes right-aligned */
            memmove(l + 7, l + 6, strlen(l + 6) + 1);
            l[6] = ' ';
        }
        if (cols - (int)strlen(l) - 1 > (int)strlen(nm) + 2)
            ctext(w, cols - (int)strlen(l) - 1, top + i, at, l);
    }
    ctext(w, 1, rows_ - 1, DIM,
          v->rec ? TR("R / Leertaste: Stopp und speichern", "R / Space: stop and save")
                 : TR("R Aufnahme  Enter Abspielen  S Stopp  Entf Loeschen",
                      "R record  Enter play  S stop  Del delete"));
}
static void vn_key(win_t *w, key_t k) {
    vn_t *v = w->st;
    int c = k.key;
    if (c == 'r' || c == 'R' || (c == ' ' && v->rec)) {
        if (v->rec)
            stop_rec(v);
        else
            start_rec(v);
    } else if (c == K_ENTER)
        start_play(v);
    else if (c == 's' || c == 'S' || c == ' ')
        stop_play(v);
    else if ((c == 'm' || c == 'M') && !v->rec) {
        inputs(v);
        if (v->ninputs > 1) {
            v->input = (v->input + 1) % v->ninputs;
            inputs(v);
            u8 b = (u8)v->input;
            fsx_write(CFG, &b, 1, fs_now());
        }
    }
    else if (c == K_UP && v->sel > 0)
        v->sel--;
    else if (c == K_DOWN && v->sel + 1 < v->nn)
        v->sel++;
    else if (c == K_HOME)
        v->sel = 0;
    else if (c == K_END && v->nn)
        v->sel = v->nn - 1;
    else if (c == K_PGUP)
        v->sel = v->sel > 8 ? v->sel - 8 : 0;
    else if (c == K_PGDN && v->nn)
        v->sel = v->sel + 8 < v->nn ? v->sel + 8 : v->nn - 1;
    else if (c == K_DEL && v->nn && !v->rec) {
        char q[120];
        ksnprintf(q, sizeof q, TR("%s loeschen?", "Delete %s?"), v->n[v->sel].name);
        if (dlg_confirm(TR("Notiz loeschen", "Delete note"), q)) {
            stop_play(v);
            char p[VFS_PATH];
            ksnprintf(p, sizeof p, "%s/%s", DIR, v->n[v->sel].name);
            fsx_remove(p);
            load_list(v, 0);
        }
    }
}
static void vn_frame(win_t *w) {
    vn_t *v = w->st;
    rec_poll(v);
    if (v->rec && v->failed)
        stop_rec(v);
    play_poll(v);
}
static void vn_tick(win_t *w) {
    vn_t *v = w->st;
    if (!v->rec)
        inputs(v); /* a microphone plugged in later */
    vn_frame(w);
}
static int vn_close(win_t *w) {
    vn_t *v = w->st;
    stop_rec(v); /* a recording is saved, not lost */
    stop_play(v);
    return 1;
}
static void vn_free(win_t *w) {
    vn_t *v = w->st;
    kfree(v->n);
    kfree(v->in);
    kfree(v->pre);
    kfree(v->out);
    kfree(v->pbuf);
    kfree(v->pd);
    kfree(v);
}
static int find_raw(const vfs_ent *e, void *a) {
    char *o = a;
    if (!e->dir && !o[0] && !memcmp(e->name, ".recording-", 11))
        ksnprintf(o, 96, "%s/%s", DIR, e->name);
    return 0;
}
static void rescue(vn_t *v) {
    char raw[96];
    raw[0] = 0;
    fsx_list(DIR, find_raw, raw);
    if (!raw[0])
        return;
    int rate = atoi(raw + strlen(DIR) + 12);
    if (rate < 8000 || rate > 192000)
        rate = 48000;
    char f[96];
    note_name(f, sizeof f, TR("Notiz gerettet", "Note recovered"));
    make_wav(v, raw, rate, f);
}
static const app_t vn_app = {vn_draw, vn_key, vn_tick, vn_close, vn_free, vn_frame};
void prog_main(void) {
    vn_t *v = kmalloc(sizeof *v);
    if (!v)
        return;
    memset(v, 0, sizeof *v);
    v->n = kmalloc(sizeof(note_t) * MAXN);
    v->in = kmalloc(9600 * 4);
    v->pre = kmalloc(PREN * 4);
    v->out = kmalloc(OUTN * 2);
    v->pbuf = kmalloc(4800 * 4);
    if (!v->n || !v->in || !v->pre || !v->out || !v->pbuf) {
        vn_free(&(win_t){.st = v});
        return;
    }
    rescue(v); /* a recording the computer did not finish */
    load_list(v, 0);
    u8 b = 0;
    if (fsx_read(CFG, &b, 1) == 1)
        v->input = b;
    inputs(v);
    win_t *w = win_open(TR("Sprachnotizen", "Voice notes"), 10, 4, 60, 16, &vn_app, v);
    if (w)
        win_menu(w, TR("Sprachnotizen", "Voice notes"), "*Info");
}
