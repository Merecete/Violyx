/* Paths and error texts of the file systems. No other dependency: the kernel, the host tests
 * and the system programs in ring 3 share this code. */
#ifdef HOST
#include <string.h>
#else
#include "str.h"
#endif
#include "viofs.h"
int vfs_norm(const char *cwd, const char *in, char *o, size_t n) {
    char t[VFS_PATH * 2];
    size_t m = 0;
    if (in[0] != '/') {
        size_t l = strlen(cwd);
        if (l >= VFS_PATH)
            return VE_INVAL;
        memcpy(t, cwd, l);
        m = l;
        t[m++] = '/';
    }
    size_t il = strlen(in);
    if (il >= VFS_PATH || m + il + 1 >= sizeof t)
        return VE_INVAL;
    memcpy(t + m, in, il + 1);
    size_t w = 0;
    if (n < 2)
        return VE_INVAL;
    o[w++] = '/';
    const char *p = t;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *e = p;
        while (*e && *e != '/')
            e++;
        size_t l = e - p;
        if (l == 1 && p[0] == '.') {
            p = e;
            continue;
        }
        if (l == 2 && p[0] == '.' && p[1] == '.') {
            if (w > 1) {
                w--;
                while (w > 1 && o[w - 1] != '/')
                    w--;
            }
            p = e;
            continue;
        }
        if (w + l + 2 >= n || w + l + 2 >= VFS_PATH)
            return VE_INVAL;
        memcpy(o + w, p, l);
        w += l;
        o[w++] = '/';
        p = e;
    }
    if (w > 1)
        w--;
    o[w] = 0;
    return 0;
}
const char *vfs_errstr(int e, int en) {
    switch (e) {
        case VE_NOENT:
            return en ? "Not found" : "Nicht gefunden";
        case VE_EXIST:
            return en ? "Already exists" : "Existiert bereits";
        case VE_NOSPC:
            return en ? "Disk full" : "Datenträger voll";
        case VE_NOTDIR:
            return en ? "Not a folder" : "Kein Ordner";
        case VE_NOTEMPTY:
            return en ? "Folder is not empty" : "Ordner ist nicht leer";
        case VE_INVAL:
            return en ? "Invalid name" : "Ungültiger Name";
        case VE_ISDIR:
            return en ? "Is a folder" : "Ist ein Ordner";
        case VE_DENIED:
            return en ? "Access not allowed" : "Zugriff nicht erlaubt";
        case VE_NOFS:
            return en ? "No writable storage here" : "Hier kein beschreibbarer Datenträger";
        case VE_FORMAT:
            return en ? "Format not supported" : "Format nicht unterstützt";
        default:
            return en ? "Disk error" : "Datenträgerfehler";
    }
}
