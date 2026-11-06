/* vxdec: entpackt Daten aus dem Netz, fest in das System eingebaut.
   Laeuft als Ring-3-Programm ohne Fenster und ohne Dateizugriff. Ein boesartiges Bild kann hoechstens
   diesen Helfer zum Absturz bringen. Der Kernel prueft alles, was er zurueckbekommt.
   Modus 0: Bild (PNG, JPEG, GIF, BMP) -> 256-Farben-Bildpunkte, 4x8 je Zeichenzelle
   Modus 1: gzip -> Klartext */
#include "violyx.h"
#include <string.h>
static inline long in_size(void){ return vx_sys(19,0,0,0); }
static inline long in_arg(int i){ return vx_sys(19,0,i,0); }
static inline void in_read(void*b,long n){ vx_sys(19,(long)b,n,0); }
static inline void out(const void*b,long n,int flag){ vx_sys(20,(long)b,n,flag); }
/* Haufen: stb_image gibt Speicher selten zurueck, ein Stapelzeiger genuegt */
typedef struct { size_t n; size_t pad; } blk;
static char *hp,*he,*last;
static void *a_malloc(size_t n){ n=(n+15)&~(size_t)15; if(n>(size_t)60<<20)return 0; while((size_t)(he-hp)<n+sizeof(blk)){ size_t g=n+sizeof(blk)>(1<<20)?n+sizeof(blk):(1<<20); char*p=vx_alloc(g); if(!p)return 0; if(!hp)hp=p; else if(p!=he)return 0; he=p+((g+4095)&~(size_t)4095); } blk*b=(blk*)hp; b->n=n; last=hp; hp+=n+sizeof(blk); return b+1; }
static void a_free(void*p){ if(!p)return; blk*b=(blk*)p-1; if((char*)b==last){ hp=last; last=0; } }
static void *a_realloc(void*p,size_t n){ if(!p)return a_malloc(n); blk*b=(blk*)p-1; if(n<=b->n)return p; size_t o=b->n; void*q=a_malloc(n); if(q)memcpy(q,p,o); return q; }
#define STBI_MALLOC a_malloc
#define STBI_FREE a_free
#define STBI_REALLOC a_realloc
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 8192
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image.h"
#define MINPX 32
static int cube(int v){ return v<48?0:v<115?1:(v-35)/40; }
static const uint8_t L[6]={0,95,135,175,215,255};
static int sq(int v){ return v*v; }
static uint8_t nearest(int r,int g,int b){ if(r<0)r=0; if(r>255)r=255; if(g<0)g=0; if(g>255)g=255; if(b<0)b=0; if(b>255)b=255; int cr=cube(r),cg=cube(g),cb=cube(b); int d=sq(r-L[cr])+sq(g-L[cg])+sq(b-L[cb]); int q=(r*3+g*6+b)/10,gi=(q-3)/10; if(gi<0)gi=0; if(gi>23)gi=23; int v=8+10*gi,e=sq(r-v)+sq(g-v)+sq(b-v); return e<d?(uint8_t)(232+gi):(uint8_t)(16+36*cr+6*cg+cb); }
static void image(const uint8_t*in,int n,int maxc,int maxr,uint32_t bg){ int w,h,c; if(!stbi_info_from_memory(in,n,&w,&h,&c)||w<1||h<1||(long)w*h>12000000L)return; if(w<MINPX||h<MINPX){ uint8_t z[4]={0,0,0,0}; out(z,4,0); return; } uint8_t*p=stbi_load_from_memory(in,n,&w,&h,&c,4); if(!p)return;
  /* Eine Zelle hat 4x8 Bildpunkte. Kleine Bilder bleiben 1:1, grosse werden passend verkleinert, der Rest bleibt Hintergrund. */
  long dw=w,dh=h; if(dw>maxc*4){ dh=dh*maxc*4/dw; dw=maxc*4; } if(dh>maxr*8){ dw=dw*maxr*8/dh; dh=maxr*8; } if(dw<1)dw=1; if(dh<1)dh=1; long cols=(dw+3)/4,rows=(dh+7)/8; long pw=cols*4,ph=rows*8; if(cols>240||rows>100)return;
  uint8_t*o=a_malloc(4+pw*ph); int*rgb=a_malloc(dw*dh*4*sizeof(int)); short*er=a_malloc((dw+2)*6*sizeof(short)); if(!o||!rgb||!er)return; o[0]=(uint8_t)cols; o[1]=(uint8_t)(cols>>8); o[2]=(uint8_t)rows; o[3]=(uint8_t)(rows>>8); memset(o+4,2,pw*ph); int br=(bg>>16)&255,bgg=(bg>>8)&255,bb=bg&255;
  for(long y=0;y<dh;y++){ long y0=y*h/dh,y1=(y+1)*h/dh; if(y1<=y0)y1=y0+1; for(long x=0;x<dw;x++){ long x0=x*w/dw,x1=(x+1)*w/dw; if(x1<=x0)x1=x0+1; long sr=0,sg=0,sb=0,sa=0,cnt=0; for(long j=y0;j<y1;j++){ const uint8_t*q=p+(j*w+x0)*4; for(long i=x0;i<x1;i++,q+=4){ int a=q[3]; sr+=(q[0]*a+br*(255-a))/255; sg+=(q[1]*a+bgg*(255-a))/255; sb+=(q[2]*a+bb*(255-a))/255; sa+=a; cnt++; } } int*t=rgb+(y*dw+x)*4; t[0]=(int)(sr/cnt); t[1]=(int)(sg/cnt); t[2]=(int)(sb/cnt); t[3]=(int)(sa/cnt); } }
  /* Fehlerstreuung: macht Verlaeufe mit 256 Farben ruhiger */
  memset(er,0,(dw+2)*6*sizeof(short)); short*e0=er,*e1=er+(dw+2)*3; for(long y=0;y<dh;y++){ memset(e1,0,(dw+2)*3*sizeof(short)); for(long x=0;x<dw;x++){ int*t=rgb+(y*dw+x)*4; if(t[3]<16)continue; int want[3]; for(int k=0;k<3;k++)want[k]=t[k]+e0[(x+1)*3+k]/16; uint8_t i=nearest(want[0],want[1],want[2]); o[4+y*pw+x]=i; int got[3]; if(i>=232)got[0]=got[1]=got[2]=8+10*(i-232); else { int v=i-16; got[0]=L[v/36]; got[1]=L[(v/6)%6]; got[2]=L[v%6]; } for(int k=0;k<3;k++){ int d=(want[k]-got[k])*3/4; if(d>60)d=60; if(d<-60)d=-60; e0[(x+2)*3+k]+=(short)(d*7); e1[x*3+k]+=(short)(d*3); e1[(x+1)*3+k]+=(short)(d*5); e1[(x+2)*3+k]+=(short)d; } } short*s=e0; e0=e1; e1=s; }
  out(o,4+pw*ph,0); }
static void gunzip(const uint8_t*in,long n,long cap){ if(n<18||in[0]!=0x1F||in[1]!=0x8B||in[2]!=8)return; int fl=in[3]; long i=10; if(fl&0xE0)return; if(fl&4){ if(i+2>n)return; i+=2+(in[i]|in[i+1]<<8); } if(fl&8){ while(i<n&&in[i])i++; i++; } if(fl&16){ while(i<n&&in[i])i++; i++; } if(fl&2)i+=2; if(i>=n)return; char*o=a_malloc((size_t)cap); if(!o)return; stbi__zbuf z; z.zbuffer=(stbi_uc*)in+i; z.zbuffer_end=(stbi_uc*)in+n; int ok=stbi__do_zlib(&z,o,(int)cap,0,0); long l=z.zout_start?z.zout-z.zout_start:0; if(l>0||ok)out(o,l,!ok); }
int main(void){ long n=in_size(); int mode=(int)in_arg(1); long a1=in_arg(2),a2=in_arg(3),a3=in_arg(4); if(n<=0||n>(48L<<20))return 0; uint8_t*in=vx_alloc((size_t)n+64); if(!in)return 0; in_read(in,n); memset(in+n,0,64); if(mode==0)image(in,(int)n,(int)a1,(int)a2,(uint32_t)a3); else if(mode==1)gunzip(in,n,a1); return 0; }
