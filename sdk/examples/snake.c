/* Snake als installierbares Programm, mit gespeichertem Rekord */
#include "violyx.h"
#define W 58
#define H 16
static int bx[W*H],by[W*H];
int main(void){ uint8_t bg=VX_ATTR(VC_BLACK,VC_DIM),sn=VX_ATTR(VC_BLACK,VC_NEON),fr=VX_ATTR(VC_BLACK,VC_HOT),tx=VX_ATTR(VC_BLACK,VC_GLOW); long best=0; vx_read("best",&best,sizeof best);
 for(;;){ int len=4,dx=1,dy=0,score=0,fx=30,fy=8; for(int i=0;i<len;i++){ bx[i]=10-i; by[i]=8; } int run=1;
  while(run){ long end=vx_ms()+(180-(score>200?80:score*2/5))*(dy?17:10)/10;   /* Zellen sind fast doppelt so hoch wie breit */ while(vx_ms()<end){ vx_wait(end-vx_ms()); int k=vx_key(); if(k==VK_UP&&!dy){ dx=0; dy=-1; } else if(k==VK_DOWN&&!dy){ dx=0; dy=1; } else if(k==VK_LEFT&&!dx){ dx=-1; dy=0; } else if(k==VK_RIGHT&&!dx){ dx=1; dy=0; } }
   int nx=bx[0]+dx,ny=by[0]+dy; if(nx<0||nx>=W||ny<0||ny>=H)run=0; for(int i=0;i<len;i++) if(bx[i]==nx&&by[i]==ny)run=0; if(!run)break; int grow=nx==fx&&ny==fy; if(grow){ len++; score+=10; fx=vx_random(W); fy=vx_random(H); } for(int i=len-1;i>0;i--){ bx[i]=bx[i-1]; by[i]=by[i-1]; } bx[0]=nx; by[0]=ny;
   vx_clear(bg); vx_text(1,0,vx_english()?"Score":"Punkte",tx); vx_number(8,0,score,tx); vx_text(16,0,vx_english()?"Best":"Rekord",tx); vx_number(23,0,best,tx); for(int x=0;x<W+2;x++){ vx_put(x,1,(char)0xC4,bg); vx_put(x,H+2,(char)0xC4,bg); } vx_put(fx+1,fy+2,(char)0x04,fr); for(int i=0;i<len;i++)vx_put(bx[i]+1,by[i]+2,(char)0xDB,i?sn:VX_ATTR(VC_BLACK,VC_GLOW)); vx_show(); }
  if(score>best){ best=score; vx_write("best",&best,sizeof best); } vx_text(18,9,vx_english()?"Game over. Enter = again":"Vorbei. Enter = nochmal",tx); vx_show(); for(;;){ vx_wait(-1); if(vx_key()==VK_ENTER)break; } } }
