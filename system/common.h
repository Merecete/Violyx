#pragma once
/* What the desktop programs of Violyx share: colours and little helpers */
#include "os.h"
#define T   AT(C_PANEL, C_ORCH)
#define DIM AT(C_PANEL, C_MID)
#define HI  AT(C_PANEL, C_NEON)
#define SEL AT(C_SEL, C_GLOW)
#define ST  AT(C_VOID, C_VIOLET)
#define SI  AT(C_VOID, C_NEON)
static inline void simple_free(win_t *w) {
    kfree(w->st);
}
static inline void sizes(u64 v, char *o, int n) {
    if (v >= 1024 * 1024)
        ksnprintf(o, n, "%ld MB", v >> 20);
    else if (v >= 1024)
        ksnprintf(o, n, "%ld KB", v >> 10);
    else
        ksnprintf(o, n, "%ld B", v);
}
static inline const char *base(const char *p) {
    const char *s = p;
    for (; *p; p++)
        if (*p == '/' && p[1])
            s = p + 1;
    return s;
}
/* Pictures the image viewer can open */
static inline int view_is_image(const char *name) {
    static const char *ext[] = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tga"};
    int l = strlen(name);
    for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++) {
        int e = strlen(ext[i]);
        if (l <= e)
            continue;
        int k = 0;
        while (k < e) {
            char c = name[l - e + k];
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (c != ext[i][k])
                break;
            k++;
        }
        if (k == e)
            return 1;
    }
    return 0;
}
