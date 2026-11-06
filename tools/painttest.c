/* Host-Test Fuellen und Bildpruefung: gcc -g -fsanitize=address,undefined -DHOST tools/painttest.c -o painttest */
#include "../kernel/paint.c"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
int main(void){ static u8 c[WSZ],f[WSZ+4]; for(int r=0;r<20000;r++){ for(int i=0;i<WSZ;i++)c[i]=rand()%(r%3+2); int x=rand()%PW,y=rand()%PH; u8 v=rand()%16; u8 o=c[y*PW+x]; pf_fill(c,x,y,v); if(o!=v)assert(c[y*PW+x]==v); memcpy(f,"VWP1",4); memcpy(f+4,c,WSZ); assert(wv_ok(f,WSZ+4)); assert(!wv_ok(f,WSZ+3)); } memset(c,0,WSZ); pf_fill(c,0,0,5); for(int i=0;i<WSZ;i++)assert(c[i]==5); puts("Malen ok"); return 0; }
