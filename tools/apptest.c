/* Host-Dauertest fuer SDK-Programme: gcc -g -fsanitize=address,undefined -Isdk tools/apptest.c PROGRAMM.c -o t
   Ersetzt das SDK durch Zufallseingaben und beendet nach N Bildern. */
#include "violyx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef memcpy
static int cw_=60,ch_=19; static long ms,frames,limit; static unsigned char held[256]; static int qk[8],qn; static int maxlvl;
long vx_sys(long n,long a,long b,long c){ switch(n){ case 2: ms+=a>0?a:10; if(rand()%3==0&&qn<8){ static const int K[]={' ','x','c','p','p',VK_ENTER,'q'}; qk[qn++]=K[rand()%7]; } return 0; case 3: return qn?qk[--qn]:-1; case 4: if(rand()%200==0){ cw_=20+rand()%218; ch_=6+rand()%90; } return (long)cw_<<16|ch_; case 7: ms+=10; return ms; case 9: if(rand()%20==0)held[a&255]=rand()%2; return held[a&255]; case 10: return 1; case 11: return -7; case 12: return 0; case 6: return 1; case 1: if(++frames>=limit){ printf("%ld Bilder ok\n",frames); exit(0); } return 0; } return 0; }
static uint16_t g; 
int vx_cols(void){ return (int)(vx_sys(4,0,0,0)>>16); } int vx_rows(void){ return (int)(vx_sys(4,0,0,0)&0xFFFF); }
void vx_clear(uint8_t a){ (void)a; } void vx_put(int x,int y,char c,uint8_t a){ if(x<-3||x>244||y<-3||y>104){ fprintf(stderr,"Zeichnung ausserhalb %d %d\n",x,y); abort(); } g+=c+a; }
void vx_text(int x,int y,const char*s,uint8_t a){ for(;*s;s++)vx_put(x++,y,*s,a); } void vx_number(int x,int y,long v,uint8_t a){ vx_put(x,y,'0'+v%10,a); }
void vx_paint(int x,int y,char c,uint8_t f,uint8_t b){ vx_put(x,y,c,f); } uint8_t vx_rgb(int r,int g,int b){ return 16; }
void vx_show(void){ vx_sys(1,0,0,0); } void vx_title(const char*s){ (void)s; }
int vx_random(int n){ if(n<=0)abort(); return rand()%n; }
__attribute__((constructor)) static void ini(void){ const char*l=getenv("FRAMES"); limit=l?atol(l):3000000; srand(getenv("SEED")?atoi(getenv("SEED")):1); }
