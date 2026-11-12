/* cc: the C compiler of Violyx. chibicc (by Rui Ueyama, MIT license) turns the C file into
 * assembly, vas makes the finished program out of it together with the C library, and the result
 * is packed as .vpk next to the source.
 *
 *   cc /home/spiel.c [-o /home/spiel.vpk] [-t Title] [-s 60x18] [-D NAME[=VALUE]] [-I folder] [-S]
 *
 * The headers and the C library are built into the compiler, so it needs nothing else. */
#include "chibicc/chibicc.h"
#include "vas.h"
#include "violyx.h"
#include "ccdata.h" /* made by tools/mkccdata.py: headers and the library as assembly */

StringArray include_paths;
bool opt_fpic;
bool opt_fcommon = true;
char *base_file;

long __os_fsize(const char *path);
#define EN vx_english()
#define TR(de, en) (EN ? (en) : (de))

char *cc_embedded(char *path) {
    for (int i = 0; i < cc_nfiles; i++)
        if (!strcmp(cc_files[i].path, path)) {
            char *b = malloc(cc_files[i].len + 1);
            if (!b)
                return 0;
            memcpy(b, cc_files[i].data, cc_files[i].len);
            b[cc_files[i].len] = 0;
            return b;
        }
    return 0;
}
static const char *embedded_text(const char *path) {
    for (int i = 0; i < cc_nfiles; i++)
        if (!strcmp(cc_files[i].path, path))
            return cc_files[i].data;
    return 0;
}
bool file_exists(char *path) {
    for (int i = 0; i < cc_nfiles; i++)
        if (!strcmp(cc_files[i].path, path))
            return true;
    return __os_fsize(path) >= 0;
}

static void usage(void) {
    printf(TR("Aufruf: cc DATEI.c [-o PROGRAMM.vpk] [-t Titel] [-s 60x18] [-D NAME] [-I Ordner] [-S]\n"
              "In Befehle: cc spiel.c   danach: run spiel.vpk\n",
              "Usage: cc FILE.c [-o PROGRAM.vpk] [-t Title] [-s 60x18] [-D NAME] [-I folder] [-S]\n"
              "In Commands: cc game.c   then: run game.vpk\n"));
    exit(1);
}
/* the 256-byte header of a .vpk, see sdk/mkvpk.py */
static void put_str(unsigned char *h, const char *s, int n) {
    int k = 0;
    for (; s[k] && k < n - 1; k++)
        h[k] = (unsigned char)s[k];
}

int main(int argc, char **argv) {
    char *in = 0, *out = 0, *title = 0, *defs[32];
    int ndef = 0, cols = 76, rows = 20, keep_s = 0;
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!strcmp(a, "-o") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(a, "-t") && i + 1 < argc)
            title = argv[++i];
        else if (!strcmp(a, "-s") && i + 1 < argc) {
            char *x;
            cols = (int)strtol(argv[++i], &x, 10);
            rows = *x == 'x' ? atoi(x + 1) : rows;
            if (cols < 20 || cols > 76 || rows < 5 || rows > 20) {
                printf(TR("Fenstergröße: 20-76 Spalten, 5-20 Zeilen\n",
                          "Window size: 20-76 columns, 5-20 rows\n"));
                exit(1);
            }
        } else if (!strncmp(a, "-D", 2) && ndef < 32)
            defs[ndef++] = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : "");
        else if (!strncmp(a, "-I", 2))
            strarray_push(&include_paths, a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : "."));
        else if (!strcmp(a, "-S"))
            keep_s = 1;
        else if (a[0] == '-')
            usage();
        else
            in = a;
    }
    if (!in)
        usage();
    size_t il = strlen(in);
    if (il < 3 || strcmp(in + il - 2, ".c")) {
        printf(TR("%s: eine C-Datei (.c) angeben\n", "%s: give a C file (.c)\n"), in);
        exit(1);
    }
    /* program name from the file name */
    char *bn = basename(strdup(in));
    char name[24], deftitle[24];
    int k = 0;
    for (char *p = bn; *p && *p != '.' && k < 23; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            c = '_';
        name[k] = c;
        deftitle[k] = *p;
        k++;
    }
    name[k] = deftitle[k] = 0;
    if (!k)
        strcpy(name, "program"), strcpy(deftitle, "Program");
    if (!title)
        title = deftitle;
    if (!out) {
        out = strdup(in);
        strcpy(out + il - 2, ".vpk");
    }

    printf(TR("Übersetze %s ...\n", "Compiling %s ...\n"), in);
    long t0 = vx_ms();
    /* the compiler: the folder of the file, then the built-in headers */
    strarray_push(&include_paths, dirname(strdup(in)));
    strarray_push(&include_paths, "/cc/include");
    init_macros();
    undef_macro("__linux");
    undef_macro("__linux__");
    undef_macro("__gnu_linux__");
    undef_macro("__unix");
    undef_macro("__unix__");
    define_macro("__violyx__", "1");
    define_macro("VX_LIBC", "1");
    for (int i = 0; i < ndef; i++) {
        char *eq = strchr(defs[i], '=');
        if (eq)
            define_macro(strndup(defs[i], (size_t)(eq - defs[i])), eq + 1);
        else
            define_macro(defs[i], "1");
    }
    base_file = in;
    Token *tok = tokenize_file(in);
    if (!tok) {
        printf(TR("%s: Datei nicht gefunden\n", "%s: file not found\n"), in);
        exit(1);
    }
    tok = preprocess(tok);
    Obj *prog = parse(tok);
    char *as;
    size_t asl;
    FILE *o = open_memstream(&as, &asl);
    codegen(prog, o);
    fclose(o);
    if (keep_s) {
        char *sp = strdup(out);
        size_t ol = strlen(sp);
        if (ol > 4 && !strcmp(sp + ol - 4, ".vpk"))
            strcpy(sp + ol - 4, ".s");
        FILE *sf = fopen(sp, "w");
        if (sf) {
            fwrite(as, 1, asl, sf);
            fclose(sf);
        }
    }

    /* assembling: start, mathematics, the C library (one file per part) and the program in one go */
    vas_file fs[24];
    int nf = 0;
    fs[nf++] = (vas_file){"crt.s", embedded_text("/cc/lib/crt.s")};
    for (int i = 0; i < cc_nfiles && nf < 23; i++)
        if (!strncmp(cc_files[i].path, "/cc/lib/", 8) && strcmp(cc_files[i].path, "/cc/lib/crt.s"))
            fs[nf++] = (vas_file){cc_files[i].path + 8, cc_files[i].data};
    fs[nf++] = (vas_file){in, as};
    vas_opts op = {0x640000400000ull, "_start", 0};
    vas_result r;
    if (vas_build(fs, nf, &op, &r)) {
        /* "undefined symbol foo": a function that is missing everywhere */
        printf(TR("Fehler beim Zusammensetzen: %s\n", "Error while building: %s\n"), r.err);
        exit(1);
    }
    unsigned char *v = calloc(1, 256 + r.elf_len);
    if (!v) {
        printf(TR("Zu wenig Speicher\n", "Out of memory\n"));
        exit(1);
    }
    memcpy(v, "VPK1", 4);
    unsigned l = (unsigned)r.elf_len;
    v[4] = (unsigned char)l, v[5] = (unsigned char)(l >> 8), v[6] = (unsigned char)(l >> 16),
    v[7] = (unsigned char)(l >> 24);
    v[8] = (unsigned char)cols, v[10] = (unsigned char)rows;
    put_str(v + 12, name, 24);
    put_str(v + 36, title, 24);
    put_str(v + 60, "1.0", 12);
    put_str(v + 72, "", 32);
    put_str(v + 104, "  cc  ", 7);
    put_str(v + 111, " .--. ", 7);
    put_str(v + 118, " '--' ", 7);
    memcpy(v + 256, r.elf, r.elf_len);
    FILE *f = fopen(out, "w");
    if (!f || fwrite(v, 1, 256 + r.elf_len, f) != 256 + r.elf_len || fclose(f)) {
        printf(TR("%s: kann nicht geschrieben werden\n", "%s: cannot be written\n"), out);
        exit(1);
    }
    long ms = vx_ms() - t0;
    printf(TR("Fertig: %s (%lu KB, %ld ms)\n", "Done: %s (%lu KB, %ld ms)\n"), out,
           (unsigned long)((256 + r.elf_len + 1023) / 1024), ms);
    printf(TR("Enter startet das Programm, Esc schließt.\n",
              "Enter starts the program, Esc closes.\n"));
    fflush(stdout);
    for (;;) {
        int c = getchar();
        if (c == '\n') {
            long e = vx_sys(30, 6, (long)out, 0);
            if (e >= 0)
                _Exit(0); /* the program runs: this window is not needed any more */
            printf(TR("Starten nicht möglich (%ld)\n", "Cannot start (%ld)\n"), e);
            return 0;
        }
        if (c < 0)
            return 0;
    }
}
