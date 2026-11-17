#include "os.h"
/* Recording: one input at a time, from a USB audio device (xhci.c) or from the HD Audio chip of
 * the mainboard (sound.c). Both put 16-bit stereo frames into one ring here, from the timer or
 * from USB events; a program takes them out with rec_read. 8 seconds fit in, so a program that
 * reads only once a second (in the background) loses nothing. */
#define CAPN (48000 * 8)
static short cap[CAPN * 2];
static volatile u64 capw; /* frames written, ever */
static u64 capr;          /* frames read */
static int src;         /* REC_USB, REC_HDA or 0 */
static void *owner;     /* the program that records */
static int rrate;
static char rname[40];
u64 rec_lost;
void rec_put(short l, short r) { /* from interrupts */
    u32 i = (u32)(capw % CAPN) * 2;
    cap[i] = l;
    cap[i + 1] = r;
    capw++;
}
int rec_active(void) {
    return src != 0;
}
/* The inputs, numbered: the USB devices (in the order they came), then the mainboard. Name of
 * input n, or 0 when there is none. */
int rec_name(int n, char *o, int max) {
    char t[40];
    for (int i = 0; usb_rec_name(i, t, sizeof t); i++)
        if (!n--) {
            ksnprintf(o, max, "USB %s", t);
            return 1;
        }
    return n == 0 && snd_rec_name(o, max);
}
/* Starts input n; when it is not there, the first one: USB first (a microphone or audio
 * interface someone plugged in on purpose), then the mainboard. Returns 0, the rate and the
 * name of the device. */
int rec_open(void *who, int n, int *rate, char *name, int max) {
    if (src)
        return VE_BUSY;
    int r = 0, nu = 0;
    char nm[40], t[40];
    nm[0] = 0;
    while (usb_rec_name(nu, t, sizeof t))
        nu++;
    if (n < 0 || !rec_name(n, t, sizeof t))
        n = 0;
    usb_rec_err[0] = 0;
    if (n < nu && !usb_rec_start(n, &r, nm, sizeof nm))
        src = REC_USB;
    else if (n >= nu && !snd_rec_start(&r, nm, sizeof nm))
        src = REC_HDA;
    else
        return rec_name(n, t, sizeof t) ? VE_IO : VE_NOENT; /* there, but did not start */
    rrate = r;
    strlcpy_(rname, nm, sizeof rname);
    owner = who;
    capr = capw;
    rec_lost = 0;
    if (rate)
        *rate = rrate;
    if (name)
        strlcpy_(name, rname, (size_t)max);
    return 0;
}
/* Takes up to max frames (16-bit stereo). Frames that waited longer than 8 seconds are gone. */
long rec_read(void *who, short *dst, u32 max) {
    if (!src || who != owner)
        return VE_INVAL;
    if (src == REC_USB && !usb_rec_alive())
        return VE_IO; /* the device was unplugged */
    u64 w = capw;
    if (w - capr > CAPN) {
        rec_lost += w - capr - CAPN;
        capr = w - CAPN;
    }
    u32 n = (u32)(w - capr);
    if (n > max)
        n = max;
    for (u32 k = 0; k < n; k++) {
        u32 i = (u32)((capr + k) % CAPN) * 2;
        dst[2 * k] = cap[i];
        dst[2 * k + 1] = cap[i + 1];
    }
    capr += n;
    return n;
}
void rec_close(void *who) {
    if (!src || (who && who != owner))
        return;
    if (src == REC_USB)
        usb_rec_stop();
    else
        snd_rec_stop();
    src = 0;
    owner = 0;
}
/* For the sound command: which inputs there are */
void rec_report(void (*cb)(const char *, void *), void *a) {
    char l[80];
    int any = 0;
    char t[40];
    for (int i = 0; rec_name(i, t, sizeof t); i++) {
        ksnprintf(l, 80, "%s %d: %s", TR("Aufnahme", "Recording"), i + 1, t);
        cb(l, a);
        any = 1;
    }
    if (!any)
        cb(TR("Kein Aufnahmegeraet gefunden", "No recording device found"), a);
    usb_rec_notes(cb, a);
    if (usb_rec_err[0]) {
        ksnprintf(l, 80, "%s %s", TR("Start fehlgeschlagen:", "Start failed:"), usb_rec_err);
        cb(l, a);
    }
    if (src) {
        ksnprintf(l, 80, TR("Nimmt gerade auf: %s, %d Hz", "Recording now: %s, %d Hz"), rname,
                  rrate);
        cb(l, a);
    }
}
