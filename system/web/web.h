#pragma once
/* The web stack of the browser, in ring 3: HTTP, TLS (Mbed TLS) and cookies. The kernel only
 * offers TCP connections and name lookups. */
#include "os.h"
extern char tls_err[80];
int tls_open(const char *host);
int tls_write(const void *, int);
int tls_read(void *, int);
void tls_close(void);
int tls_weak(void);
typedef struct {
    int status, length, tls, redirects, truncated, cert_bad;
    char type[60], err[80], final_host[120], final_url[1200], fname[64];
} http_res;
int http_get(const char *url, const char *post, char *buf, int max, http_res *r,
             void (*progress)(const char *));
enum {
    HTTP_NOCOOKIE = 1,
    HTTP_NOGZIP = 2
};
int http_fetch(const char *url, const char *post, char *buf, int max, http_res *r,
               void (*progress)(const char *), int flags);
void cookie_set(const char *host, const char *path, const char *line, int len, int tls);
int cookie_get(const char *host, const char *path, int tls, char *out, int max);
void cookie_clear(void);
void cookie_save(void);
int cookie_count(void);
