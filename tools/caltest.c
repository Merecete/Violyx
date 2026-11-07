/* Host test for the calendar: gcc -g -fsanitize=address,undefined -DHOST tools/caltest.c -o
 * caltest; the output is cross-checked with Python */
#include "../kernel/calendar.c"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
static cstore A, B;
static char b[80000];
int main(void) {
    for (int y = 1980; y <= 2099; y++)
        for (int m = 1; m <= 12; m++)
            for (int d = 1; d <= cal_days(y, m); d++)
                printf("%d %d %d %d\n", y, m, d, cal_wday(y, m, d));
    for (int r = 0; r < 3000; r++) {
        A.n = 0;
        int n = rand() % 40;
        for (int i = 0; i < n; i++) {
            int y = 1980 + rand() % 120, m = 1 + rand() % 12, d = 1 + rand() % cal_days(y, m);
            char t[61];
            int l = 1 + rand() % 60;
            for (int k = 0; k < l; k++)
                t[k] = (char)(32 + rand() % 90);
            t[l] = 0;
            assert(!cal_set(&A, y * 10000 + m * 100 + d, t));
            if (rand() % 5 == 0)
                cal_del(&A, y * 10000 + m * 100 + d);
        }
        for (int i = 1; i < A.n; i++)
            assert(A.a[i - 1].date < A.a[i].date);
        long o = cal_dump(&A, b, sizeof b);
        cal_parse(&B, b, o);
        assert(B.n == A.n);
        for (int i = 0; i < A.n; i++) {
            assert(B.a[i].date == A.a[i].date);
            assert(!strcmp(B.a[i].t, A.a[i].t));
        }
        for (int k = 0; k < 20; k++)
            b[rand() % (o + 1)] = (char)rand();
        cal_parse(&B, b, o);
        for (int i = 1; i < B.n; i++)
            assert(B.a[i - 1].date < B.a[i].date);
    }
    for (int i = 0; i < CN_MAX + 5; i++) {
        int r = cal_set(&A, 19800101 + 0 * i, "x");
        (void)r;
    }
    A.n = 0;
    for (int i = 0; i < CN_MAX; i++)
        assert(!cal_set(&A, 20000000 + i * 100 + 1, "x"));
    assert(cal_set(&A, 21000101, "x"));
    fprintf(stderr, "Logik ok\n");
    return 0;
}
