#include "os.h"
/* xHCI (USB 3) mit Tastaturen im HID-Bootprotokoll. Bis zu vier Controller, Hubs werden unterstuetzt.
   Der Treiber arbeitet immer auf dem Controller UH; jeder Einstieg setzt ihn passend.
   Geraete werden im Leerlauf ein- und ausgehaengt. Die Berichte
   werden in PS/2-Scancodes uebersetzt und laufen durch denselben Tastaturtreiber. */
typedef struct { u32 a,b,c,d; } trb;
typedef struct { trb*t; int i,cy,n; } ring;
#define MAXK 4
struct msc; 
typedef struct ud { int kind,slot,tried,hn,speed,depth,rport,tts,ttp,vid,pid,cls; u32 route; struct msc*m; ring ep0; struct ud*kid[16]; u8 ktr[16]; } ud_t;
#define MAXP 64
#define MAXHC 4
typedef struct hc { u64 cap,op,rt,db; int csz,ready,evi,evc,nports; u64*dcba; ring cmd; trb*ev; ud_t pt[MAXP]; } hc_t;
static hc_t hcs[MAXHC]; static int nhc; static hc_t*UH=&hcs[0];
#define cap (UH->cap)
#define op (UH->op)
#define rt (UH->rt)
#define db (UH->db)
#define csz (UH->csz)
#define ready (UH->ready)
#define evi (UH->evi)
#define evc (UH->evc)
#define nports (UH->nports)
#define dcba (UH->dcba)
#define cmd (UH->cmd)
#define ev (UH->ev)
#define pt (UH->pt)
typedef struct { hc_t*h; int slot,dci; ring r; u8*rep; u8 last[8]; } kb_t;
static kb_t kb[MAXK];
int usb_keyboards; static int rep_sc; static u64 rep_at;
#define C32(o) (*(volatile u32*)(cap+(o)))
#define O32(o) (*(volatile u32*)(op+(o)))
#define O64(o) (*(volatile u64*)(op+(o)))
#define R32(o) (*(volatile u32*)(rt+(o)))
#define R64(o) (*(volatile u64*)(rt+(o)))
#define DB(s)  (*(volatile u32*)(db+(s)*4))
#define EVN 256
#define PS(n) (*(volatile u32*)(op+0x400+(n)*0x10))
static int want,waitport,cur_kind,cur_slot,cur_hn,cur_vid,cur_pid,cur_cls; static void*cur_m; static ring cur_ep0;
static void ring_new(ring*r,int n){ r->t=dma_alloc(n*sizeof(trb)); r->n=n;r->i=0;r->cy=1; u64 p=phys(r->t); trb*l=&r->t[n-1]; l->a=p;l->b=p>>32;l->d=(6<<10)|2; }
static void put_trb(ring*r,u32 a,u32 b,u32 c,u32 d){ trb*t=&r->t[r->i]; t->a=a;t->b=b;t->c=c; __asm__("":::"memory"); t->d=d|r->cy; if(++r->i==r->n-1){ r->t[r->n-1].d=(r->t[r->n-1].d&~1u)|r->cy; r->cy^=1; r->i=0; } }
static int ev_next(trb*o){ volatile trb*e=&ev[evi]; if((e->d&1)!=(u32)evc)return 0; *o=*(trb*)e; if(++evi==EVN){evi=0;evc^=1;} R64(0x38)=phys(&ev[evi])|8; return 1; }
static void kb_event(trb*e);
#define URN 1024
#define UFN 32
typedef struct { int ifn,alt,dci,fdci,ch,sub,upt,async,maxn,bsz,fbn,errs,lastcc,stalls; ring r,fr; u8*buf,*fbuf; u64 seq[URN],qd,dn,lastdn,lastmove; u32 acc,fb,nom,pos; char fmt[48]; } ust;
typedef struct { hc_t*h; int slot,on,uac2,fs,unitrate,sidx,setrate,vid,pid,nst; ust st[2]; char err[48]; } ua_t;
static ua_t UA; static int ua_found;
static void ua_event(trb*e);
/* Wartet auf ein Ereignis; Tastatur-Ereignisse werden dabei weiterverarbeitet */
static int ev_wait(int type,trb*o){ for(int i=0;i<6000000;i++){ if(ev_next(o)){ int t=(o->d>>10)&63; if(t==32){ int s=o->d>>24,kbd=0; if(UA.on&&UA.h==UH&&s==UA.slot){ ua_event(o); continue; } for(int k=0;k<usb_keyboards;k++) if(kb[k].h==UH&&kb[k].slot==s)kbd=1; if(kbd){ kb_event(o); continue; } if(want&&s!=want)continue; } if(t==type)return 1; } if(waitport&&!(i&1023)&&!(PS(waitport-1)&1))return 0; udelay(1); } return 0; }
static int command(u32 a,u32 b,u32 c,u32 d){ put_trb(&cmd,a,b,c,d); DB(0)=0; trb e; if(!ev_wait(33,&e))return -1; return (e.c>>24)==1?(int)(e.d>>24):-1; }
typedef struct msc { hc_t*h; int slot,in,out,port; ring ri,ro,ep0; u8*buf; u8*cbw; u32 tag; blkdev*dev; } msc_t;
static int ctrl(int slot,ring*r,u32 rq,u16 val,u16 idx,u16 len,void*buf){ int in=rq&0x80; put_trb(r,rq|(val&0xFFFFu)<<16|0,idx|(u32)len<<16,8,(2<<10)|(1<<6)|((len?(in?3:2):0)<<16));
  if(len){ u64 p=phys(buf); put_trb(r,p,p>>32,len,(3<<10)|((in?1:0)<<16)); } put_trb(r,0,0,0,(4<<10)|(1<<5)|((len&&in?0:1)<<16)); DB(slot)=1; want=slot; trb e; if(!ev_wait(32,&e))return -1; u32 cc=e.c>>24; return cc==1||cc==13?0:-1; }
static int log2i(int v){ int n=0; while(v>1){v>>=1;n++;} return n; }
/* Bulk-Only-Transport */
static int bulk(msc_t*m,int in,void*b,u32 len,int*cc){ ring*r=in?&m->ri:&m->ro; u64 p=phys(b); put_trb(r,p,p>>32,len,(1<<10)|(1<<5)|(in?1<<2:0)); DB(m->slot)=in?m->in:m->out; want=m->slot; trb e; if(!ev_wait(32,&e))return -1; *cc=e.c>>24; return (*cc==1||*cc==13)?0:-1; }
static void unstall(msc_t*m,int in){ int dci=in?m->in:m->out; ring*r=in?&m->ri:&m->ro; command(0,0,0,(14<<10)|(m->slot<<24)|(dci<<16)); u64 q=phys(&r->t[r->i])|r->cy; command(q,q>>32,0,(16<<10)|(m->slot<<24)|(dci<<16)); ctrl(m->slot,&m->ep0,0x02|(1<<8),0,in?(m->in/2)|0x80:m->out/2,0,0); }
static int scsi(msc_t*m,const u8*cdb,int cl,u32 len,int in){ u8*c=m->cbw; memset(c,0,31); memcpy(c,"USBC",4); *(u32*)(c+4)=++m->tag; *(u32*)(c+8)=len; c[12]=in?0x80:0; c[14]=cl; memcpy(c+15,cdb,cl); int cc=0;
  if(bulk(m,0,c,31,&cc)){ unstall(m,0); return -1; } if(len&&bulk(m,in,m->buf,len,&cc)&&cc==6)unstall(m,in);
  u8*s=m->cbw+32; if(bulk(m,1,s,13,&cc)){ if(cc==6){ unstall(m,1); if(bulk(m,1,s,13,&cc))return -1; } else return -1; } return memcmp(s,"USBS",4)||*(u32*)(s+4)!=m->tag||s[12]?-1:0; }
static u64 irq_off(void){ u64 f; __asm__ volatile("pushfq; pop %0; cli":"=r"(f)); return f; }
static void irq_on(u64 f){ __asm__ volatile("push %0; popfq"::"r"(f)); }
static int rw10(msc_t*m,u8 o_,u64 l){ u8 c[10]={o_,0,l>>24,l>>16,l>>8,l,0,0,1,0}; return scsi(m,c,10,512,o_==0x28); }
static int m_ok(blkdev*d,msc_t*m){ if(d->gone)return 0; UH=m->h; waitport=m->port; return 1; }
static int m_rd(blkdev*d,u64 l,void*b){ msc_t*m=d->p; u64 f=irq_off(); int r=-1; if(m_ok(d,m)){ r=rw10(m,0x28,l); if(!r)memcpy(b,m->buf,512); waitport=0; } irq_on(f); return r; }
static int m_wr(blkdev*d,u64 l,const void*b){ msc_t*m=d->p; u64 f=irq_off(); int r=-1; if(m_ok(d,m)){ memcpy(m->buf,b,512); r=rw10(m,0x2A,l); waitport=0; } irq_on(f); return r; }
static int m_fl(blkdev*d){ msc_t*m=d->p; u64 f=irq_off(); if(m_ok(d,m)){ u8 c[10]={0x35}; scsi(m,c,10,0,0); waitport=0; } irq_on(f); return 0; }
static void msc_start(msc_t*m){ u8 tur[6]={0},sense[6]={3,0,0,0,18,0},inq[6]={0x12,0,0,0,36,0}; int ok=0; static char mdl[26]; mdl[0]=0; if(!scsi(m,inq,6,36,1)){ int k=0; for(int i=8;i<32;i++){ if(i==16&&k&&mdl[k-1]!=' ')mdl[k++]=' '; u8 c=m->buf[i]; if(c<32||c>126)c=' '; if(c==' '&&(!k||mdl[k-1]==' '))continue; mdl[k++]=(char)c; } while(k&&mdl[k-1]==' ')k--; mdl[k]=0; } for(int i=0;i<40&&!ok;i++){ ok=!scsi(m,tur,6,0,0); if(!ok){ scsi(m,sense,6,18,1); udelay(100000); } } if(!ok)return;
  u8 c[10]={0x25}; if(scsi(m,c,10,8,1))return; u8*b=m->buf; u32 last=b[0]<<24|b[1]<<16|b[2]<<8|b[3],bs=b[4]<<24|b[5]<<16|b[6]<<8|b[7]; if(bs==512){ m->dev=blk_register("USB",(u64)last+1,m,m_rd,m_wr,m_fl); if(m->dev){ m->dev->usb=1; strlcpy_(m->dev->model,mdl,41); } } }
#define IC(i) ((u32*)(ictx+(i)*csz))
/* ---------- USB-Audio (Klasse 1 und 2): Wiedergabe ueber einen isochronen Endpunkt ----------
   Der Mischer in sound.c schreibt in einen Ring, ua_feed() packt daraus Pakete im Takt des Geraets.
   Bei asynchronen Geraeten meldet ein Rueckkanal (Feedback), wie viele Bildpunkte pro Paket es wirklich will. */
int snd_add_usb(const char*); void snd_usb_gone(int);
static void ep0_fix(int slot,ring*r){ command(0,0,0,(14<<10)|((u32)slot<<24)|(1<<16)); u64 q=phys(&r->t[r->i])|r->cy; command(q,q>>32,0,(16<<10)|((u32)slot<<24)|(1<<16)); }
static int uctrl(int slot,ring*r,u32 rq,u16 val,u16 idx,u16 len,void*buf){ int e=ctrl(slot,r,rq,val,idx,len,buf); if(e)ep0_fix(slot,r); return e; }
static void ua_fb_queue(ust*u){ int i=u->fr.i; u64 p=phys(u->fbuf+i*8); put_trb(&u->fr,p,p>>32,u->fbn,(5<<10)|(1<<5)|(1<<2)|(1u<<31)); }
static void ua_event(trb*e){ int dci=(e->d>>16)&31; u32 cc=e->c>>24; u64 ptr=e->a|(u64)e->b<<32;
  for(int s=0;s<UA.nst;s++){ ust*u=&UA.st[s];
    if(dci==u->dci){ u64 b=phys(u->r.t); if(ptr>=b&&ptr<b+URN*16){ int idx=(int)((ptr-b)/16); u->dn=u->seq[idx]+1; } if(cc==14||cc==15)u->dn=u->qd; else if(cc!=1&&cc!=13){ u->errs++; u->lastcc=(int)cc; } return; }
    if(u->fdci&&dci==u->fdci){ u64 b=phys(u->fr.t); if(ptr>=b&&ptr<b+UFN*16&&(cc==1||cc==13)){ int idx=(int)((ptr-b)/16); u8*v=u->fbuf+idx*8; u32 f=UA.fs?((u32)v[0]|(u32)v[1]<<8|(u32)v[2]<<16)<<2:((u32)v[0]|(u32)v[1]<<8|(u32)v[2]<<16|(u32)v[3]<<24); u32 n=u->nom; for(int k=0;k<4&&f;k++){ if(f>n-n/8&&f<n+n/8)break; if(f>n)f>>=3; else f<<=3; } if(f>n-n/8&&f<n+n/8)u->fb=f; } ua_fb_queue(u); DB(UA.slot)=u->fdci; return; } } }
static int ua_fill(ust*u,const short*src,u32 fr){ if(u->dn!=u->lastdn){ u->lastdn=u->dn; u->lastmove=ticks; } else if(u->qd!=u->dn&&ticks>u->lastmove+30){ u->dn=u->qd; u->stalls++; u->lastmove=ticks; }
  u64 target=(u64)UA.unitrate*40/1000/u->upt; if(target<8)target=8; if(target>URN-40)target=URN-40; int put=0;
  while(u->qd-u->dn<target){ int idx=u->r.i; u->acc+=(u->fb?u->fb:u->nom)*u->upt; int n=u->acc>>16; u->acc&=0xFFFF; if(n>u->maxn)n=u->maxn; u8*o=u->buf+idx*u->bsz; int fsz=u->ch*u->sub;
    for(int k=0;k<n;k++){ const short*s=src+((u->pos+k)%fr)*2; u8*f=o+k*fsz; memset(f,0,fsz); for(int c=0;c<u->ch&&c<2;c++){ short v=u->ch==1?(short)((s[0]+s[1])/2):s[c]; f[c*u->sub+u->sub-2]=(u8)v; f[c*u->sub+u->sub-1]=(u8)((u16)v>>8); } }
    u->pos=(u->pos+n)%fr; u64 p=phys(o); put_trb(&u->r,p,p>>32,(u32)(n*fsz),(5<<10)|(((u->qd&7)==7)?1<<5:0)|(1u<<31)); u->seq[idx]=u->qd; u->qd++; put=1; }
  return put; }
/* Fuellt Pakete aus dem Mischring; laeuft im Timer. Der erste Ausgang gibt das Tempo vor, ein zweiter (vorne) folgt ihm */
void usb_audio_feed(const short*src,u32 fr,u32*pos){ if(!UA.on)return; hc_t*sv=UH; UH=UA.h; ust*a=&UA.st[0]; a->pos=*pos; if(ua_fill(a,src,fr))DB(UA.slot)=a->dci; *pos=a->pos;
  if(UA.nst>1){ ust*b=&UA.st[1]; int d=(int)((b->pos+fr-a->pos)%fr); if(d>(int)fr/2)d-=(int)fr; if(d>1500||d<-1500)b->pos=a->pos; if(ua_fill(b,src,fr))DB(UA.slot)=b->dci; } UH=sv; }
void usb_audio_info(void(*cb)(const char*,void*),void*a,int fail){ char l[80]; if(!ua_found||fail==UA.on)return; if(UA.on){ for(int s=0;s<UA.nst;s++){ ust*u=&UA.st[s]; ksnprintf(l,80,TR("        Ausgang %d: %s","        output %d: %s"),s+1,u->fmt); cb(l,a); ksnprintf(l,80,TR("        Pakete %lu, Fehler %d (zuletzt %d), Haenger %d, Takt %s","        packets %lu, errors %d (last %d), stalls %d, clock %s"),u->qd,u->errs,u->lastcc,u->stalls,!u->async?"-":u->fb?TR("geregelt","feedback"):TR("geschaetzt","estimated")); cb(l,a); } }
  else { ksnprintf(l,80,TR("USB-Audio %04x:%04x gefunden, aber: %s","USB audio %04x:%04x found, but: %s"),UA.vid,UA.pid,UA.err); cb(l,a); } }
static void ua_gone(void){ if(UA.on){ UA.on=0; snd_usb_gone(UA.sidx); } ua_found=0; }
/* Wertet die Beschreibung aus und startet die Wiedergabe. Rueckgabe 1, wenn es ein Audiogeraet war */
static int ua_setup(int slot,ring*ep0,u8*octx,u8*ictx,int speed,const u8*d,int tl,int cfgv){
  int acif=-1,uac2=0,clk[4],nclk=0,fu[8],nfu=0; int ci=-1,ca=0,ccls=0,csub=0;
  typedef struct { int ifn,alt,ch,sub,ok48,pcm,ep,mps,ival,attr,fb,fbmps,fbival,sc; } cand; cand c,cs[4]; int nc=0; memset(&c,0,sizeof c);
#define TAKE() do{ if(ccls==1&&csub==2&&ca>0&&c.ep&&c.pcm&&c.ch>=1&&c.ch<=8&&(c.sub==2||c.sub==3||c.sub==4)&&c.ok48){ c.sc=(c.ch==2)*4+(c.sub==2)*2+1; int k=0; for(;k<nc;k++) if(cs[k].ifn==c.ifn)break; if(k<nc){ if(c.sc>cs[k].sc)cs[k]=c; } else if(nc<4)cs[nc++]=c; } }while(0)
  for(int o=0;o+1<tl&&d[o];o+=d[o]){ const u8*x=d+o; int L=x[0];
    if(x[1]==4&&L>=9){ TAKE(); ci=x[2]; ca=x[3]; ccls=x[5]; csub=x[6]; memset(&c,0,sizeof c); c.ifn=ci; c.alt=ca; if(ccls==1&&csub==1){ acif=ci; if(x[7]==0x20)uac2=1; } if(ccls==1&&csub==2&&x[7]==0x20)uac2=1; continue; }
    if(x[1]==0x24&&ccls==1&&csub==1&&L>=4){ if(x[2]==1&&L>=5&&x[4]>=2)uac2=1; if(x[2]==0x0A&&nclk<4)clk[nclk++]=x[3]; if(x[2]==6&&nfu<8)fu[nfu++]=x[3]; }
    if(x[1]==0x24&&ccls==1&&csub==2&&L>=4){ if(x[2]==1){ if(uac2&&L>=11){ c.pcm=x[6]&1; c.ch=x[10]; } else if(L>=7)c.pcm=(x[5]|x[6]<<8)==1; } if(x[2]==2){ if(uac2&&L>=6){ c.sub=x[4]; c.ok48=1; } else if(L>=8&&x[3]==1){ c.ch=x[4]; c.sub=x[5]; int nf=x[7]; if(!nf&&L>=14){ u32 lo=x[8]|x[9]<<8|x[10]<<16,hi=x[11]|x[12]<<8|x[13]<<16; c.ok48=lo<=48000&&hi>=48000; } for(int k=0;k<nf&&8+k*3+2<L;k++) if((u32)(x[8+k*3]|x[9+k*3]<<8|x[10+k*3]<<16)==48000)c.ok48=1; } } }
    if(x[1]==5&&ccls==1&&csub==2&&L>=7&&(x[3]&3)==1){ if(!(x[2]&0x80)&&!c.ep){ c.ep=x[2]; c.mps=x[4]|x[5]<<8; c.ival=x[6]; c.attr=x[3]; if(!uac2&&L>=9&&x[8])c.fb=x[8]; } else if((x[2]&0x80)&&((x[3]>>4)&3)==1){ c.fb=x[2]; c.fbmps=x[4]|x[5]<<8; c.fbival=x[6]; } else if((x[2]&0x80)&&c.fb==x[2]){ c.fbmps=x[4]|x[5]<<8; c.fbival=x[6]; } } }
  TAKE();
#undef TAKE
  if(acif<0)return 0; ua_found=1; memset(&UA,0,sizeof UA); UA.vid=cur_vid; UA.pid=cur_pid;
  if(!nc){ strlcpy_(UA.err,TR("keine passende 48-kHz-Ausgabe","no matching 48 kHz output"),48); return 1; }
  if(ctrl(slot,ep0,0|(9<<8),cfgv,0,0,0)){ strlcpy_(UA.err,"SET_CONFIGURATION",48); return 1; }
  u8*t=dma_alloc(64); if(uac2){ for(int i=0;i<nclk;i++){ t[0]=0x80; t[1]=0xBB; t[2]=0; t[3]=0; if(!uctrl(slot,ep0,0x21|(1<<8),0x0100,(u16)(clk[i]<<8|acif),4,t))UA.setrate++; } }
  int hs=speed>=3; UA.fs=!hs; UA.unitrate=hs?8000:1000; UA.uac2=uac2; if(nc>2)nc=2;
  int top=0; memset(ictx,0,33*csz); memcpy(IC(1),octx,csz); IC(0)[1]=1;
  for(int s=0;s<nc;s++){ cand*b=&cs[s]; ust*u=&UA.st[UA.nst];
    if(uctrl(slot,ep0,0x01|(11<<8),(u16)b->alt,(u16)b->ifn,0,0)){ if(!UA.nst)strlcpy_(UA.err,"SET_INTERFACE",48); continue; }
    if(!uac2){ t[0]=0x80; t[1]=0xBB; t[2]=0; if(!uctrl(slot,ep0,0x22|(1<<8),0x0100,(u16)b->ep,3,t))UA.setrate++; }
    int iv=b->ival<1?1:b->ival>16?16:b->ival; u->upt=1<<(iv-1); u->ifn=b->ifn; u->alt=b->alt; u->ch=b->ch; u->sub=b->sub; u->async=((b->attr>>2)&3)==1; u->nom=(u32)(((u64)48000<<16)/UA.unitrate);
    int mps=b->mps&0x7FF,burst=hs?(b->mps>>11)&3:0,esit=mps*(burst+1); u->maxn=esit/(b->ch*b->sub); int need=(int)((48000ull*u->upt+UA.unitrate-1)/UA.unitrate)+2; if(u->maxn>need)u->maxn=need; if(u->maxn<1)continue; u->bsz=(u->maxn*b->ch*b->sub+63)&~63;
    u->dci=(b->ep&15)*2; u->fdci=(u->async&&b->fb&&b->fbmps)?(b->fb&15)*2+1:0; ring_new(&u->r,URN); u->buf=dma_alloc(URN*u->bsz); if(u->fdci){ ring_new(&u->fr,UFN); u->fbuf=dma_alloc(UFN*8); u->fbn=b->fbmps<4?b->fbmps:(hs?4:3); }
    IC(0)[1]|=1u<<u->dci; if(u->dci>top)top=u->dci; if(u->fdci){ IC(0)[1]|=1u<<u->fdci; if(u->fdci>top)top=u->fdci; }
    int xiv=hs?iv-1:iv-1+3; u32*e=IC(u->dci+1); e[0]=(u32)xiv<<16|(u32)((esit>>16)&255)<<24; e[1]=(1<<3)|((u32)burst<<8)|((u32)mps<<16); u64 q=phys(u->r.t); e[2]=q|1; e[3]=q>>32; e[4]=(u32)(u->maxn*b->ch*b->sub)|((u32)(esit&0xFFFF)<<16);
    if(u->fdci){ int fiv=b->fbival<1?1:b->fbival>16?16:b->fbival; e=IC(u->fdci+1); e[0]=(u32)(hs?fiv-1:fiv-1+3)<<16; e[1]=(5<<3)|((u32)(b->fbmps&0x7FF)<<16); q=phys(u->fr.t); e[2]=q|1; e[3]=q>>32; e[4]=(u32)u->fbn|((u32)(b->fbmps&0x7FF)<<16); }
    ksnprintf(u->fmt,48,"UAC%d %s, %d %s %d Bit, %s",uac2?2:1,hs?"480M":"12M",b->ch,TR("Kanaele","ch"),b->sub*8,u->async?"async":((b->attr>>2)&3)==2?"adaptive":"sync"); UA.nst++; }
  if(!UA.nst){ if(!UA.err[0])strlcpy_(UA.err,TR("Paketgroesse zu klein","packet size too small"),48); return 1; }
  for(int i=0;i<nfu;i++) for(int chn=0;chn<=2;chn++){ t[0]=0; uctrl(slot,ep0,0x21|(1<<8),(u16)(1<<8|chn),(u16)(fu[i]<<8|acif),1,t); t[0]=0; t[1]=0; uctrl(slot,ep0,0x21|(1<<8),(u16)(2<<8|chn),(u16)(fu[i]<<8|acif),2,t); }
  IC(1)[0]=(IC(1)[0]&0x07FFFFFF)|((u32)top<<27);
  if(command(phys(ictx),phys(ictx)>>32,0,(12<<10)|((u32)slot<<24))<0){ strlcpy_(UA.err,TR("Endpunkt nicht eingerichtet","endpoint setup failed"),48); UA.nst=0; return 1; }
  UA.h=UH; UA.slot=slot; for(int s=0;s<UA.nst;s++){ ust*u=&UA.st[s]; if(u->fdci){ for(int i=0;i<UFN-2;i++)ua_fb_queue(u); DB(slot)=u->fdci; } }
  char nm[40]; ksnprintf(nm,40,"%04x:%04x",cur_vid,cur_pid); UA.sidx=snd_add_usb(nm); if(UA.sidx<0){ strlcpy_(UA.err,TR("zu viele Soundgeraete","too many sound devices"),48); return 1; } UA.on=1; cur_kind=4; return 1; }
static void usb_dev(int port,int speed,u32 route,int tts,int ttp,int depth){ u32 slot; { put_trb(&cmd,0,0,0,9<<10); DB(0)=0; trb e; if(!ev_wait(33,&e)||(e.c>>24)!=1)return; slot=e.d>>24; cur_slot=slot; }
  u8*ictx=dma_alloc(33*csz),*octx=dma_alloc(32*csz); dcba[slot]=phys(octx); ring ep0; ring_new(&ep0,32); int mps=speed==4?512:speed==3?64:8;
  IC(0)[1]=3; IC(1)[0]=route|(speed<<20)|(1u<<27); IC(1)[1]=port<<16; IC(1)[2]=tts|(ttp<<8); IC(2)[1]=(3<<1)|(4<<3)|(mps<<16); u64 p=phys(ep0.t); IC(2)[2]=p|1; IC(2)[3]=p>>32; IC(2)[4]=8;
  if(command(phys(ictx),phys(ictx)>>32,0,(11<<10)|(slot<<24))<0)return;
  static u8*d; if(!d)d=dma_alloc(4096); if(ctrl(slot,&ep0,0x80|(6<<8),0x100,0,8,d))return;
  int real=speed==4?1<<d[7]:d[7]; if(real&&real!=mps){ memset(ictx,0,33*csz); IC(0)[1]=2; IC(2)[1]=(3<<1)|(4<<3)|(real<<16); IC(2)[2]=p|1; IC(2)[3]=p>>32; if(command(phys(ictx),phys(ictx)>>32,0,(13<<10)|(slot<<24))<0)return; }
  cur_cls=d[4]; if(!ctrl(slot,&ep0,0x80|(6<<8),0x100,0,18,d)){ cur_vid=d[8]|d[9]<<8; cur_pid=d[10]|d[11]<<8; }
  if(ctrl(slot,&ep0,0x80|(6<<8),0x200,0,9,d))return; int tl=d[2]|d[3]<<8; if(tl>4096)tl=4096; if(ctrl(slot,&ep0,0x80|(6<<8),0x200,0,tl,d))return; int cfgv=d[5];
  int ifn=-1,epa=0,epi=0,eps=8,cur=0; for(int o=0;o+1<tl&&d[o];o+=d[o]){ if(d[o+1]==4){ cur=d[o+5]==3&&d[o+6]==1&&d[o+7]==1; if(cur&&ifn<0)ifn=d[o+2]; } else if(d[o+1]==5&&cur&&(d[o+2]&0x80)&&(d[o+3]&3)==3&&!epa){ epa=d[o+2]&15; eps=d[o+4]|d[o+5]<<8; epi=d[o+6]; } }
  if(cur_cls!=9&&!ua_found&&ua_setup(slot,&ep0,octx,ictx,speed,d,tl,cfgv)){ cur_ep0=ep0; return; }
  /* Hub: Slot als Hub kennzeichnen und Ports einschalten */
  if(cur_cls==9){ if(ctrl(slot,&ep0,0|(9<<8),cfgv,0,0,0))return; int ss=speed==4; if(ss)ctrl(slot,&ep0,0x20|(12<<8),depth,0,0,0); if(ctrl(slot,&ep0,0xA0|(6<<8),(ss?0x2A:0x29)<<8,0,9,d))return; int hn=d[2],pw=d[5]; if(hn>15)hn=15; memset(ictx,0,33*csz); IC(0)[1]=1; memcpy(IC(1),octx,csz); IC(1)[0]|=1u<<26; IC(1)[1]=(IC(1)[1]&0x00FFFFFF)|((u32)hn<<24); command(phys(ictx),phys(ictx)>>32,0,(12<<10)|(slot<<24)); for(int i=1;i<=hn;i++)ctrl(slot,&ep0,0x23|(3<<8),8,i,0,0); udelay((pw?pw:50)*2000+20000); cur_hn=hn; cur_kind=3; cur_ep0=ep0; return; }
  /* USB-Speicher: Klasse 8, SCSI, Bulk-Only */
  int msi=-1,mi=0,mo=0,mpi=0,mpo=0,bi=0,bo=0,in_ms=0,last=0; for(int o=0;o+1<tl&&d[o];o+=d[o]){ if(d[o+1]==4){ in_ms=d[o+5]==8&&d[o+6]==6&&d[o+7]==0x50; if(in_ms&&msi<0)msi=d[o+2]; last=0; }
    else if(d[o+1]==5&&in_ms&&(d[o+3]&3)==2){ int n=d[o+2]&15,m=d[o+4]|d[o+5]<<8; if(d[o+2]&0x80){ if(!mi){mi=n;mpi=m;last=1;} } else if(!mo){mo=n;mpo=m;last=2;} } else if(d[o+1]==0x30&&in_ms){ if(last==1)bi=d[o+2]; else if(last==2)bo=d[o+2]; } }
  if(msi>=0&&mi&&mo&&ifn<0){ if(ctrl(slot,&ep0,0|(9<<8),cfgv,0,0,0))return; msc_t*m=dma_alloc(sizeof*m); cur_m=m; cur_kind=2; m->h=UH; m->slot=slot; m->port=port; m->in=mi*2+1; m->out=mo*2; m->ep0=ep0; ring_new(&m->ri,64); ring_new(&m->ro,64); m->buf=dma_alloc(4096); m->cbw=dma_alloc(64);
    int top=m->in>m->out?m->in:m->out; memset(ictx,0,33*csz); IC(0)[1]=1|(1u<<m->in)|(1u<<m->out); memcpy(IC(1),octx,csz); IC(1)[0]=(IC(1)[0]&0x07FFFFFF)|((u32)top<<27);
    u32*e=IC(m->in+1); e[1]=(3<<1)|(6<<3)|(bi<<8)|(mpi<<16); u64 q=phys(m->ri.t); e[2]=q|1;e[3]=q>>32;e[4]=512; e=IC(m->out+1); e[1]=(3<<1)|(2<<3)|(bo<<8)|(mpo<<16); q=phys(m->ro.t); e[2]=q|1;e[3]=q>>32;e[4]=512;
    if(command(phys(ictx),phys(ictx)>>32,0,(12<<10)|(slot<<24))<0)return; msc_start(m); return; }
  if(ifn<0||!epa||usb_keyboards>=MAXK)return;
  if(ctrl(slot,&ep0,0|(9<<8),cfgv,0,0,0))return; ctrl(slot,&ep0,0x21|(0x0B<<8),0,ifn,0,0); ctrl(slot,&ep0,0x21|(0x0A<<8),0,ifn,0,0);
  kb_t*k=&kb[usb_keyboards]; memset(k,0,sizeof*k); k->h=UH; k->slot=slot; k->dci=epa*2+1; ring_new(&k->r,32); k->rep=dma_alloc(64); int iv=speed>=3?(epi?epi-1:0):3+log2i(epi?epi:1); if(iv>15)iv=15;
  memset(ictx,0,33*csz); IC(0)[1]=1|(1u<<k->dci); memcpy(IC(1),(u8*)octx,csz); IC(1)[0]=(IC(1)[0]&0x07FFFFFF)|((u32)k->dci<<27); u32*e=IC(k->dci+1); e[0]=iv<<16; e[1]=(3<<1)|(7<<3)|(eps<<16); u64 q=phys(k->r.t); e[2]=q|1;e[3]=q>>32;e[4]=8|(8u<<16);
  if(command(phys(ictx),phys(ictx)>>32,0,(12<<10)|(slot<<24))<0)return;
  q=phys(k->rep); put_trb(&k->r,q,q>>32,8,(1<<10)|(1<<5)); DB(slot)=k->dci; usb_keyboards++; cur_kind=1;
}
/* Buchfuehrung fuer Ein- und Ausstecken, auch hinter Hubs */
static ud_t kids[48]; static u8 kid_used[48]; static int usb_ch; int usb_hold;
static ud_t*kid_new(void){ for(int i=0;i<48;i++) if(!kid_used[i]){ kid_used[i]=1; memset(&kids[i],0,sizeof kids[i]); return &kids[i]; } return 0; }
static void slot_free(int slot){ if(!slot)return; want=0; command(0,0,0,(10<<10)|((u32)slot<<24)); dcba[slot]=0; }
static void dev_gone(ud_t*q){ for(int i=1;i<16;i++) if(q->kid[i]){ ud_t*k=q->kid[i]; dev_gone(k); kid_used[k-kids]=0; q->kid[i]=0; q->ktr[i]=0; } if(q->kind==2&&q->m&&q->m->dev)blk_gone(q->m->dev); if(q->kind==4||(ua_found&&UA.h==UH&&UA.slot==q->slot))ua_gone(); if(q->kind==1){ for(int i=0;i<usb_keyboards;i++) if(kb[i].h==UH&&kb[i].slot==q->slot){ kb[i]=kb[--usb_keyboards]; rep_sc=0; break; } } slot_free(q->slot); q->kind=0; q->slot=0; q->m=0; q->hn=0; q->cls=q->vid=q->pid=0; usb_ch=1; }
static void hub_poll(ud_t*q);
static void dev_attach(ud_t*q,int rport,int speed,u32 route,int tts,int ttp,int depth){ cur_kind=0; cur_slot=0; cur_m=0; cur_hn=0; cur_vid=cur_pid=0; cur_cls=-1; usb_dev(rport,speed,route,tts,ttp,depth); q->kind=cur_kind; q->slot=cur_slot; q->m=cur_m; q->speed=speed; q->rport=rport; q->route=route; q->tts=tts; q->ttp=ttp; q->depth=depth; q->hn=cur_hn; q->ep0=cur_ep0; q->vid=cur_vid; q->pid=cur_pid; q->cls=cur_cls; usb_ch=1; if(!cur_kind&&cur_slot){ slot_free(cur_slot); q->slot=0; } if(q->kind==3)hub_poll(q); }
static void hub_poll(ud_t*q){ static u8*b; if(!b)b=dma_alloc(16); int ss=q->speed==4; for(int p=1;p<=q->hn&&q->kind==3;p++){ waitport=q->rport; int r=ctrl(q->slot,&q->ep0,0xA3,0,p,4,b); waitport=0; if(r)continue; u32 st=b[0]|b[1]<<8,chg=b[2]|b[3]<<8; int csc=chg&1; if(csc)ctrl(q->slot,&q->ep0,0x23|(1<<8),16,p,0,0); if((csc||!(st&1))&&q->kid[p]){ ud_t*k=q->kid[p]; dev_gone(k); kid_used[k-kids]=0; q->kid[p]=0; } if(csc||!(st&1))q->ktr[p]=0; if(!(st&1)||q->ktr[p])continue; q->ktr[p]=1; if(q->depth>=4)continue;
    ctrl(q->slot,&q->ep0,0x23|(3<<8),4,p,0,0); int ok=0; for(int i=0;i<100;i++){ udelay(10000); if(ctrl(q->slot,&q->ep0,0xA3,0,p,4,b))break; if((b[2]|b[3]<<8)&0x10){ ok=1; break; } } ctrl(q->slot,&q->ep0,0x23|(1<<8),20,p,0,0); if(!ok)continue; udelay(20000); if(ctrl(q->slot,&q->ep0,0xA3,0,p,4,b))continue; st=b[0]|b[1]<<8; if(!(st&2))continue; int sp=ss?4:(st&(1<<9))?2:(st&(1<<10))?3:1; ud_t*k=kid_new(); if(!k)continue; q->kid[p]=k; u32 rt_=q->route|((u32)p<<(4*q->depth)); int tts=q->tts,ttp=q->ttp; if(q->speed==3&&sp<=2){ tts=q->slot; ttp=p; } dev_attach(k,q->rport,sp,rt_,tts,ttp,q->depth+1); } for(int p=1;p<=q->hn;p++) if(q->kid[p]&&q->kid[p]->kind==3)hub_poll(q->kid[p]); }
static void port_attach(int n){ u32 s=PS(n); if(!(s&1))return; if(!(s&2)){ PS(n)=(s&0x0E00C200)|(1<<4); for(int i=0;i<200000&&!(PS(n)&(1<<21));i++)udelay(5); udelay(20000); s=PS(n); if(!(s&2))return; }
  PS(n)=(s&0x0E00C200)|(0x7F<<17); dev_attach(&pt[n],n+1,(PS(n)>>10)&15,0,0,0,0); }
void usb_hotplug(void){ static u64 next; if(usb_hold||ticks<next||!nhc)return; next=ticks+50; u64 f=irq_off(); hc_t*sv=UH; usb_ch=0; for(int c=0;c<nhc;c++){ UH=&hcs[c]; if(!ready)continue; for(int n=0;n<nports;n++){ u32 s=PS(n); int csc=s&(1<<17); if(csc)PS(n)=(s&0x0E00C200)|(1<<17); if((csc||!(s&1))&&pt[n].slot)dev_gone(&pt[n]); if(csc||!(s&1))pt[n].tried=0; if((s&1)&&!pt[n].tried){ pt[n].tried=1; port_attach(n); } else if(pt[n].kind==3)hub_poll(&pt[n]); } } UH=sv; irq_on(f); if(usb_ch)drives_sync(); }
static const char*ud_name(ud_t*q){ return q->kind==1?TR("Tastatur","keyboard"):q->kind==2?TR("Speicher","storage"):q->kind==3?"Hub":q->kind==4?"Audio":TR("nicht unterstützt","not supported"); }
static const char*ud_sp(int s){ return s==4?"5G":s==3?"480M":s==2?"1.5M":"12M"; }
static void ud_line(ud_t*q,const char*pre,void(*cb)(const char*,void*),void*a){ char l[72]; ksnprintf(l,72,"%s %s %s %04x:%04x cl%d",pre,ud_name(q),ud_sp(q->speed),q->vid,q->pid,q->cls); cb(l,a); for(int i=1;i<16;i++) if(q->kid[i]){ char p[24]; ksnprintf(p,24,"%s.%d",pre,i); ud_line(q->kid[i],p,cb,a); } }
void usb_report(void(*cb)(const char*,void*),void*a){ char l[72]; if(!nhc){ cb(TR("Kein USB-3-Controller gefunden.","No USB 3 controller found."),a); return; } u64 f=irq_off(); hc_t*sv=UH; for(int c=0;c<nhc;c++){ UH=&hcs[c]; ksnprintf(l,72,TR("Controller %d: %d Ports","Controller %d: %d ports"),c+1,nports); cb(l,a); for(int n=0;n<nports;n++){ if(PS(n)&1){ char p[16]; ksnprintf(p,16,"  %d",n+1); ud_t*q=&pt[n]; if(q->slot||q->cls>=0)ud_line(q,p,cb,a); else { ksnprintf(l,72,TR("  %d nicht angemeldet","  %d not enumerated"),n+1); cb(l,a); } } } } UH=sv; irq_on(f); }
void xhci_init(const pci_t*c){ if(nhc>=MAXHC)return; UH=&hcs[nhc]; pci_enable(c); u64 bar=pci_bar(c,0); if(!bar)return; mmio_map(bar,0x10000); cap=bar; op=cap+(C32(0)&255); rt=cap+(C32(0x18)&~31u); db=cap+(C32(0x14)&~3u);
  u32 hc1=C32(0x10),hs1=C32(4),hs2=C32(8); csz=(hc1&4)?64:32; for(u32 x=hc1>>16,o=x*4;x;){ u64 a=cap+o; u32 v=*(volatile u32*)a; if((v&255)==1){ *(volatile u32*)a=v|(1<<24); for(int i=0;i<500000&&(*(volatile u32*)a&(1<<16));i++)udelay(2); } x=(v>>8)&255; o+=x*4; }
  O32(0)&=~1u; for(int i=0;i<200000&&!(O32(4)&1);i++)udelay(5); O32(0)|=2; for(int i=0;i<200000&&(O32(0)&2);i++)udelay(5); for(int i=0;i<200000&&(O32(4)&(1<<11));i++)udelay(5);
  int slots=hs1&255,ports=hs1>>24; if(ports>MAXP)ports=MAXP; nports=ports; if(slots>32)slots=32; O32(0x38)=slots; dcba=dma_alloc(2048); int sp=((hs2>>21)&31)<<5|(hs2>>27); memset(pt,0,sizeof pt); if(sp){ u64*a=dma_alloc(sp*8); for(int i=0;i<sp;i++)a[i]=phys(dma_alloc(4096)); dcba[0]=phys(a); }
  O64(0x30)=phys(dcba); ring_new(&cmd,64); O64(0x18)=phys(cmd.t)|1;
  ev=dma_alloc(EVN*sizeof(trb)); u64*er=dma_alloc(16); er[0]=phys(ev); ((u32*)er)[2]=EVN; evc=1; R32(0x28)=1; R64(0x38)=phys(ev)|8; R64(0x30)=phys(er);
  O32(0)|=1; for(int i=0;i<200000&&(O32(4)&1);i++)udelay(5);
#define PKEEP 0x4F01C3E0u  /* Bits, die beim Schreiben erhalten bleiben; Aenderungsbits nicht */
  for(int n=0;n<ports;n++){ u32 s=PS(n); if(!(s&(1<<9))){ PS(n)=(s&0x0E00C200)|(1<<9); udelay(20000); s=PS(n); } if(!(s&1))continue; pt[n].tried=1; port_attach(n); }
  ready=1; nhc++; }
/* HID-Nutzungscode -> Satz-1-Scancode, 0x100 = E0-Praefix */
static const u16 map[101]={[4]=0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C,
 2,3,4,5,6,7,8,9,10,11,0x1C,1,0x0E,0x0F,0x39,0x0C,0x0D,0x1A,0x1B,0x2B,0x2B,0x27,0x28,0x29,0x33,0x34,0x35,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44,0x57,0x58,
 [73]=0x152,0x147,0x149,0x153,0x14F,0x151,0x14D,0x14B,0x150,0x148,[84]=0x135,0x37,0x4A,0x4E,0x11C,0x4F,0x50,0x51,0x4B,0x4C,0x4D,0x47,0x48,0x49,0x52,0x53,0x56};
static const u16 modc[8]={0x1D,0x2A,0x38,0,0x11D,0x36,0x138,0};
static void sc(int c,int up){ if(c&0x100)kbd_in(0xE0); kbd_in((c&0x7F)|(up?0x80:0)); }
static int has(const u8*r,u8 u){ for(int i=2;i<8;i++) if(r[i]==u)return 1; return 0; }
static void report(kb_t*k,const u8*r){ if(r[2]==1)return; for(int b=0;b<8;b++){ int was=k->last[0]>>b&1,now=r[0]>>b&1; if(was!=now&&modc[b])sc(modc[b],!now); }
  for(int i=2;i<8;i++){ u8 u=k->last[i]; if(u&&u<=100&&map[u]&&!has(r,u)){ sc(map[u],1); if(rep_sc==map[u])rep_sc=0; } }
  for(int i=2;i<8;i++){ u8 u=r[i]; if(u&&u<=100&&map[u]&&!has(k->last,u)){ sc(map[u],0); rep_sc=map[u]; rep_at=ticks+50; } } memcpy(k->last,r,8); }
static void kb_event(trb*e){ int s=e->d>>24; for(int i=0;i<usb_keyboards;i++) if(kb[i].h==UH&&kb[i].slot==s){ kb_t*k=&kb[i]; u32 cc=e->c>>24; if(cc==1||cc==13)report(k,k->rep); u64 q=phys(k->rep); put_trb(&k->r,q,q>>32,8,(1<<10)|(1<<5)); DB(s)=k->dci; } }
void xhci_poll(void){ hc_t*sv=UH; trb e; for(int c=0;c<nhc;c++){ UH=&hcs[c]; if(!ready)continue; while(ev_next(&e)){ if(((e.d>>10)&63)==32){ if(UA.on&&UA.h==UH&&(int)(e.d>>24)==UA.slot)ua_event(&e); else kb_event(&e); } } } UH=sv;
  if(rep_sc&&ticks>=rep_at){ sc(rep_sc,0); rep_at=ticks+3; } }
const char*usb_status(void){ return usb_keyboards?"USB-Tastatur":"keine"; }
