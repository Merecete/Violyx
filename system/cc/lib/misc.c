/* C library of Violyx: time and assertions */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "sys.h"
time_t time(time_t *t) {
    time_t v = __os_time();
    if (t)
        *t = v;
    return v;
}
clock_t clock(void) {
    return (clock_t)__os_ms() * (CLOCKS_PER_SEC / 1000);
}
static struct tm tmv;
struct tm *gmtime(const time_t *t) {
    long long s = *t, d = s / 86400, r = s % 86400;
    if (r < 0) {
        r += 86400;
        d--;
    }
    tmv.tm_hour = (int)(r / 3600);
    tmv.tm_min = (int)(r / 60 % 60);
    tmv.tm_sec = (int)(r % 60);
    tmv.tm_wday = (int)((d + 4) % 7 + 7) % 7;
    /* days to a date (Howard Hinnant's algorithm) */
    long long z = d + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153, dd = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    tmv.tm_year = (int)(y - 1900);
    tmv.tm_mon = (int)(m - 1);
    tmv.tm_mday = (int)dd;
    static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int leap = (y % 4 == 0 && y % 100) || y % 400 == 0;
    tmv.tm_yday = cum[m - 1] + (int)dd - 1 + (leap && m > 2);
    tmv.tm_isdst = 0;
    return &tmv;
}
struct tm *localtime(const time_t *t) {
    return gmtime(t);
}
void __assert_fail(const char *e, const char *f, int l) {
    fprintf(stderr, "%s:%d: assertion failed: %s\n", f, l, e);
    abort();
}
/* libgen: the folder and the name of a path (they change the string, like the originals may) */
char *dirname(char *p) {
    if (!p || !*p)
        return ".";
    char *s = p + strlen(p) - 1;
    while (s > p && *s == '/')
        *s-- = 0;
    while (s >= p && *s != '/')
        s--;
    if (s < p)
        return ".";
    while (s > p && *s == '/')
        s--;
    s[1] = 0;
    return p;
}
char *basename(char *p) {
    if (!p || !*p)
        return ".";
    char *s = p + strlen(p) - 1;
    while (s > p && *s == '/')
        *s-- = 0;
    char *b = strrchr(p, '/');
    return b && b[1] ? b + 1 : p;
}
