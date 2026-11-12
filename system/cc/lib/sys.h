/* What the C library needs from the system below it: Violyx (vx.c) or, for tests of the
 * compiler on the build computer, Linux (linux.c) */
long __os_write(int fd, const char *b, long n); /* standard output and error */
void __os_exit(int code);
void *__os_sbrk(long n); /* more memory at the end of the heap, 0 if there is none */
long __os_fsize(const char *path); /* size of a file, -1 if it is not there */
long __os_fread(const char *path, void *b, long n);
long __os_fwrite(const char *path, const void *b, long n); /* the whole file */
int __os_remove(const char *path);
int __os_rename(const char *a, const char *b);
long __os_time(void); /* seconds since 1970 */
long __os_ms(void);   /* milliseconds since the start */
int __os_getchar(void); /* standard input, -1 at the end */
void __os_init(void);
void __os_fini(int code); /* before the end: shows the output until a key */
int __os_args(char ***argv); /* argc, with argv */
