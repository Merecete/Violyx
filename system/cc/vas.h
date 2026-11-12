#pragma once
/* vas: the assembler of Violyx. It reads the assembly that the C compiler writes (AT&T syntax, the
 * x86-64 subset of chibicc plus what hand-written runtime code needs) and builds the finished
 * program right away: all files together, symbols resolved across them, no object files and no
 * linker. The result is a static ELF executable at a fixed address. */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name; /* for messages */
    const char *text; /* NUL-terminated assembly */
} vas_file;

typedef struct {
    uint64_t base;     /* address of the first segment (Violyx: 0x640000400000) */
    const char *entry; /* symbol the program starts at, e.g. "_start" */
    int listing;       /* test mode: undefined symbols are 0, a list of bytes per line is made */
} vas_opts;

typedef struct {
    uint8_t *elf; /* the executable (malloc), 0 on error */
    size_t elf_len;
    char err[256]; /* first error, "file:line: message" */
    char *list;    /* listing mode: "file:line: bytes" lines (malloc) */
    size_t list_len;
    uint64_t text_size, data_size, bss_size;
} vas_result;

int vas_build(const vas_file *files, int n, const vas_opts *o, vas_result *r);
