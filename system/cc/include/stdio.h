#ifndef __STDIO_H
#define __STDIO_H
#include <stddef.h>
#include <stdarg.h>
/* On Violyx, standard output and error appear in the window of the program, standard input reads
 * from the keyboard. A file is read completely when it is opened and written when it is closed. */
typedef struct __file FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define BUFSIZ 1024
#define FILENAME_MAX 256
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int vprintf(const char *, va_list);
int vfprintf(FILE *, const char *, va_list);
int vsprintf(char *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
int scanf(const char *, ...);
int sscanf(const char *, const char *, ...);
int vsscanf(const char *, const char *, va_list);
int putchar(int);
int puts(const char *);
int fputc(int, FILE *);
int putc(int, FILE *);
int fputs(const char *, FILE *);
int getchar(void);
int fgetc(FILE *);
int getc(FILE *);
int ungetc(int, FILE *);
char *fgets(char *, int, FILE *);
FILE *fopen(const char *, const char *);
int fclose(FILE *);
int fflush(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
void rewind(FILE *);
int feof(FILE *);
int ferror(FILE *);
void clearerr(FILE *);
int remove(const char *);
int rename(const char *, const char *);
void perror(const char *);
FILE *open_memstream(char **, size_t *);
#endif
