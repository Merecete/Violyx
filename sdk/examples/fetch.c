/* Fetch: loads a web page over plain HTTP and shows the first lines of the answer. Shows how a
 * program uses the network: name lookup and TCP come from the kernel, the protocol (here HTTP,
 * for HTTPS a TLS library) is part of the program. Violyx asks once whether it may go online. */
#include "violyx.h"
static char host[64] = "example.com", buf[8192];
static int n;
static void line(int y, const char *s, uint8_t a) {
    vx_text(1, y, s, a);
}
static void load(void) {
    uint32_t ip;
    n = 0;
    buf[0] = 0;
    vx_clear(VX_ATTR(VC_BLACK, VC_LAVENDER));
    line(1, vx_english() ? "Looking up the name ..." : "Suche den Namen ...", VX_ATTR(0, VC_MID));
    vx_show();
    int r = vx_resolve(host, &ip);
    if (!r) {
        line(2, vx_english() ? "Connecting ..." : "Verbinde ...", VX_ATTR(0, VC_MID));
        vx_show();
        r = vx_connect(ip, 80);
    }
    if (r) {
        const char *e =
            r == VX_DENIED
                ? (vx_english() ? "Not allowed to go online." : "Darf nicht ins Internet.")
            : r == VX_BUSY ? (vx_english() ? "The network is busy." : "Das Netzwerk ist belegt.")
                           : (vx_english() ? "No connection." : "Keine Verbindung.");
        n = 0;
        for (const char *p = e; *p; p++)
            buf[n++] = *p;
        buf[n] = 0;
        return;
    }
    char req[160];
    int k = 0;
    const char *a = "GET / HTTP/1.0\r\nHost: ", *b = "\r\nUser-Agent: Violyx-Fetch\r\n\r\n";
    for (const char *p = a; *p; p++)
        req[k++] = *p;
    for (const char *p = host; *p; p++)
        req[k++] = *p;
    for (const char *p = b; *p; p++)
        req[k++] = *p;
    for (int o = 0; o < k;) {
        long s = vx_send(req + o, k - o);
        if (s < 0)
            break;
        if (!s)
            vx_wait(10);
        o += (int)s;
    }
    long end = vx_ms() + 10000;
    while (n < (int)sizeof buf - 1 && vx_ms() < end) {
        long g = vx_recv(buf + n, sizeof buf - 1 - n);
        if (g == VX_AGAIN) {
            vx_wait(10);
            continue;
        }
        if (g <= 0)
            break;
        n += (int)g;
    }
    buf[n] = 0;
    vx_disconnect();
}
static void draw(void) {
    vx_clear(VX_ATTR(VC_BLACK, VC_LAVENDER));
    vx_text(1, 0, host, VX_ATTR(VC_BLACK, VC_NEON));
    int y = 2, x = 1;
    for (int i = 0; i < n && y < vx_rows() - 1; i++) {
        char c = buf[i];
        if (c == '\n' || x >= vx_cols() - 1) {
            y++;
            x = 1;
            if (c == '\n')
                continue;
        }
        if (c >= 32 && c < 127)
            vx_put(x++, y, c, VX_ATTR(VC_BLACK, VC_LAVENDER));
    }
    vx_text(1, vx_rows() - 1, vx_english() ? "Enter: other address" : "Enter: andere Adresse",
            VX_ATTR(VC_BLACK, VC_DIM));
    vx_show();
}
int main(void) {
    load();
    for (;;) {
        draw();
        vx_wait(-1);
        int k = vx_key();
        if (k == VK_ENTER &&
            vx_input("Fetch", vx_english() ? "Host name:" : "Rechnername:", host, sizeof host))
            load();
    }
}
