#include "os.h"
/* xHCI (USB 3) mit Tastaturen im HID-Bootprotokoll. Direkt an den Hauptports,
   noch ohne USB-Hubs. Geraete werden im Leerlauf ein- und ausgehaengt. Die Berichte
   werden in PS/2-Scancodes uebersetzt und laufen durch denselben Tastaturtreiber. */
typedef struct { u32 a,b,c,d; } trb;
typedef struct { trb*t; int i,cy,n; } ring;
#define MAXK 4
typedef struct { int slot,dci; ring r; u8*rep; u8 last[8]; } kb_t;
static u64 cap,op,rt,db; static int csz,ready; static u64*dcba; static ring cmd; static trb*ev; static int evi,evc; static kb_t kb[MAXK];
int usb_keyboards; static int rep_sc; static u64 rep_at;
#define C32(o) (*(volatile u32*)(cap+(o)))
#define O32(o) (*(volatile u32*)(op+(o)))
#define O64(o) (*(volatile u64*)(op+(o)))
#define R32(o) (*(volatile u32*)(rt+(o)))
#define R64(o) (*(volatile u64*)(rt+(o)))
#define DB(s)  (*(volatile u32*)(db+(s)*4))
#define EVN 256
#define PS(n) (*(volatile u32*)(op+0x400+(n)*0x10))
static int want,waitport,cur_kind,cur_slot; static void*cur_m;
static void ring_new(ring*r,int n){ r->t=dma_alloc(n*sizeof(trb)); r->n=n;r->i=0;r->cy=1; u64 p=phys(r->t); trb*l=&r->t[n-1]; l->a=p;l->b=p>>32;l->d=(6<<10)|2; }
static void put_trb(ring*r,u32 a,u32 b,u32 c,u32 d){ trb*t=&r->t[r->i]; t->a=a;t->b=b;t->c=c; __asm__("":::"memory"); t->d=d|r->cy; if(++r->i==r->n-1){ r->t[r->n-1].d=(r->t[r->n-1].d&~1u)|r->cy; r->cy^=1; r->i=0; } }
static int ev_next(trb*o){ volatile trb*e=&ev[evi]; if((e->d&1)!=(u32)evc)return 0; *o=*(trb*)e; if(++evi==EVN){evi=0;evc^=1;} R64(0x38)=phys(&ev[evi])|8; return 1; }
static void kb_event(trb*e);
/* Wartet auf ein Ereignis; Tastatur-Ereignisse werden dabei weiterverarbeitet */
static int ev_wait(int type,trb*o){ for(int i=0;i<6000000;i++){ if(ev_next(o)){ int t=(o->d>>10)&63; if(t==32){ int s=o->d>>24,kbd=0; for(int k=0;k<usb_keyboards;k++) if(kb[k].slot==s)kbd=1; if(kbd){ kb_event(o); continue; } if(want&&s!=want)continue; } if(t==type)return 1; } if(waitport&&!(i&1023)&&!(PS(waitport-1)&1))return 0; udelay(1); } return 0; }
static int command(u32 a,u32 b,u32 c,u32 d){ put_trb(&cmd,a,b,c,d); DB(0)=0; trb e; if(!ev_wait(33,&e))return -1; return (e.c>>24)==1?(int)(e.d>>24):-1; }
typedef struct { int slot,in,out,port; ring ri,ro,ep0; u8*buf; u8*cbw; u32 tag; blkdev*dev; } msc_t;
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
static int rw10(msc_t*m,u8 op,u64 l){ u8 c[10]={op,0,l>>24,l>>16,l>>8,l,0,0,1,0}; return scsi(m,c,10,512,op==0x28); }
static int m_ok(blkdev*d,msc_t*m){ if(d->gone)return 0; waitport=m->port; return 1; }
static int m_rd(blkdev*d,u64 l,void*b){ msc_t*m=d->p; u64 f=irq_off(); int r=-1; if(m_ok(d,m)){ r=rw10(m,0x28,l); if(!r)memcpy(b,m->buf,512); waitport=0; } irq_on(f); return r; }
static int m_wr(blkdev*d,u64 l,const void*b){ msc_t*m=d->p; u64 f=irq_off(); int r=-1; if(m_ok(d,m)){ memcpy(m->buf,b,512); r=rw10(m,0x2A,l); waitport=0; } irq_on(f); return r; }
static int m_fl(blkdev*d){ msc_t*m=d->p; u64 f=irq_off(); if(m_ok(d,m)){ u8 c[10]={0x35}; scsi(m,c,10,0,0); waitport=0; } irq_on(f); return 0; }
static void msc_start(msc_t*m){ u8 tur[6]={0},sense[6]={3,0,0,0,18,0},inq[6]={0x12,0,0,0,36,0}; int ok=0; static char mdl[26]; mdl[0]=0; if(!scsi(m,inq,6,36,1)){ int k=0; for(int i=8;i<32;i++){ if(i==16&&k&&mdl[k-1]!=' ')mdl[k++]=' '; u8 c=m->buf[i]; if(c<32||c>126)c=' '; if(c==' '&&(!k||mdl[k-1]==' '))continue; mdl[k++]=(char)c; } while(k&&mdl[k-1]==' ')k--; mdl[k]=0; } for(int i=0;i<40&&!ok;i++){ ok=!scsi(m,tur,6,0,0); if(!ok){ scsi(m,sense,6,18,1); udelay(100000); } } if(!ok)return;
  u8 c[10]={0x25}; if(scsi(m,c,10,8,1))return; u8*b=m->buf; u32 last=b[0]<<24|b[1]<<16|b[2]<<8|b[3],bs=b[4]<<24|b[5]<<16|b[6]<<8|b[7]; if(bs==512){ m->dev=blk_register("USB",(u64)last+1,m,m_rd,m_wr,m_fl); if(m->dev){ m->dev->usb=1; strlcpy_(m->dev->model,mdl,41); } } }
static void usb_dev(int port,int speed){ u32 slot; { put_trb(&cmd,0,0,0,9<<10); DB(0)=0; trb e; if(!ev_wait(33,&e)||(e.c>>24)!=1)return; slot=e.d>>24; cur_slot=slot; }
  u8*ictx=dma_alloc(33*csz),*octx=dma_alloc(32*csz); dcba[slot]=phys(octx); ring ep0; ring_new(&ep0,32); int mps=speed==4?512:speed==3?64:8;
#define IC(i) ((u32*)(ictx+(i)*csz))
  IC(0)[1]=3; IC(1)[0]=(speed<<20)|(1u<<27); IC(1)[1]=port<<16; IC(2)[1]=(3<<1)|(4<<3)|(mps<<16); u64 p=phys(ep0.t); IC(2)[2]=p|1; IC(2)[3]=p>>32; IC(2)[4]=8;
  if(command(phys(ictx),phys(ictx)>>32,0,(11<<10)|(slot<<24))<0)return;
  u8*d=dma_alloc(512); if(ctrl(slot,&ep0,0x80|(6<<8),0x100,0,8,d))return;
  int real=speed==4?1<<d[7]:d[7]; if(real&&real!=mps){ memset(ictx,0,33*csz); IC(0)[1]=2; IC(2)[1]=(3<<1)|(4<<3)|(real<<16); IC(2)[2]=p|1; IC(2)[3]=p>>32; if(command(phys(ictx),phys(ictx)>>32,0,(13<<10)|(slot<<24))<0)return; }
  if(ctrl(slot,&ep0,0x80|(6<<8),0x200,0,9,d))return; int tl=d[2]|d[3]<<8; if(tl>512)tl=512; if(ctrl(slot,&ep0,0x80|(6<<8),0x200,0,tl,d))return; int cfgv=d[5];
  int ifn=-1,epa=0,epi=0,eps=8,cur=0; for(int o=0;o+1<tl&&d[o];o+=d[o]){ if(d[o+1]==4){ cur=d[o+5]==3&&d[o+6]==1&&d[o+7]==1; if(cur&&ifn<0)ifn=d[o+2]; } else if(d[o+1]==5&&cur&&(d[o+2]&0x80)&&(d[o+3]&3)==3&&!epa){ epa=d[o+2]&15; eps=d[o+4]|d[o+5]<<8; epi=d[o+6]; } }
  /* USB-Speicher: Klasse 8, SCSI, Bulk-Only */
  int msi=-1,mi=0,mo=0,mpi=0,mpo=0,bi=0,bo=0,in_ms=0,last=0; for(int o=0;o+1<tl&&d[o];o+=d[o]){ if(d[o+1]==4){ in_ms=d[o+5]==8&&d[o+6]==6&&d[o+7]==0x50; if(in_ms&&msi<0)msi=d[o+2]; last=0; }
    else if(d[o+1]==5&&in_ms&&(d[o+3]&3)==2){ int n=d[o+2]&15,m=d[o+4]|d[o+5]<<8; if(d[o+2]&0x80){ if(!mi){mi=n;mpi=m;last=1;} } else if(!mo){mo=n;mpo=m;last=2;} } else if(d[o+1]==0x30&&in_ms){ if(last==1)bi=d[o+2]; else if(last==2)bo=d[o+2]; } }
  if(msi>=0&&mi&&mo&&ifn<0){ if(ctrl(slot,&ep0,0|(9<<8),cfgv,0,0,0))return; msc_t*m=dma_alloc(sizeof*m); cur_m=m; cur_kind=2; m->slot=slot; m->port=port; m->in=mi*2+1; m->out=mo*2; m->ep0=ep0; ring_new(&m->ri,64); ring_new(&m->ro,64); m->buf=dma_alloc(4096); m->cbw=dma_alloc(64);
    int top=m->in>m->out?m->in:m->out; memset(ictx,0,33*csz); IC(0)[1]=1|(1u<<m->in)|(1u<<m->out); memcpy(IC(1),octx,csz); IC(1)[0]=(IC(1)[0]&0x07FFFFFF)|((u32)top<<27);
    u32*e=IC(m->in+1); e[1]=(3<<1)|(6<<3)|(bi<<8)|(mpi<<16); u64 q=phys(m->ri.t); e[2]=q|1;e[3]=q>>32;e[4]=512; e=IC(m->out+1); e[1]=(3<<1)|(2<<3)|(bo<<8)|(mpo<<16); q=phys(m->ro.t); e[2]=q|1;e[3]=q>>32;e[4]=512;
    if(command(phys(ictx),phys(ictx)>>32,0,(12<<10)|(slot<<24))<0)return; msc_start(m); return; }
  if(ifn<0||!epa||usb_keyboards>=MAXK)return;
  if(ctrl(slot,&ep0,0|(9<<8),cfgv,0,0,0))return; ctrl(slot,&ep0,0x21|(0x0B<<8),0,ifn,0,0); ctrl(slot,&ep0,0x21|(0x0A<<8),0,ifn,0,0);
  kb_t*k=&kb[usb_keyboards]; k->slot=slot; k->dci=epa*2+1; ring_new(&k->r,32); k->rep=dma_alloc(64); int iv=speed>=3?(epi?epi-1:0):3+log2i(epi?epi:1); if(iv>15)iv=15;
  memset(ictx,0,33*csz); IC(0)[1]=1|(1u<<k->dci); memcpy(IC(1),(u8*)octx,csz); IC(1)[0]=(IC(1)[0]&0x07FFFFFF)|((u32)k->dci<<27); u32*e=IC(k->dci+1); e[0]=iv<<16; e[1]=(3<<1)|(7<<3)|(eps<<16); u64 q=phys(k->r.t); e[2]=q|1;e[3]=q>>32;e[4]=8|(8u<<16);
  if(command(phys(ictx),phys(ictx)>>32,0,(12<<10)|(slot<<24))<0)return;
  q=phys(k->rep); put_trb(&k->r,q,q>>32,8,(1<<10)|(1<<5)); DB(slot)=k->dci; usb_keyboards++; cur_kind=1;
}
/* Port-Buchfuehrung fuer Ein- und Ausstecken */
#define MAXP 64
typedef struct { int kind,slot,tried; msc_t*m; } pt_t;
static pt_t pt[MAXP]; static int nports; int usb_hold;
static void slot_free(int slot){ if(!slot)return; want=0; command(0,0,0,(10<<10)|((u32)slot<<24)); dcba[slot]=0; }
static void port_attach(int n){ u32 s=PS(n); if(!(s&1))return; if(!(s&2)){ PS(n)=(s&0x0E00C200)|(1<<4); for(int i=0;i<200000&&!(PS(n)&(1<<21));i++)udelay(5); udelay(20000); s=PS(n); if(!(s&2))return; }
  PS(n)=(s&0x0E00C200)|(0x7F<<17); cur_kind=0; cur_slot=0; cur_m=0; usb_dev(n+1,(PS(n)>>10)&15); pt[n].kind=cur_kind; pt[n].slot=cur_slot; pt[n].m=cur_m; if(!cur_kind&&cur_slot){ slot_free(cur_slot); pt[n].slot=0; } }
static void port_gone(int n){ pt_t*q=&pt[n]; if(q->kind==2&&q->m&&q->m->dev)blk_gone(q->m->dev); if(q->kind==1){ for(int i=0;i<usb_keyboards;i++) if(kb[i].slot==q->slot){ kb[i]=kb[--usb_keyboards]; rep_sc=0; break; } } slot_free(q->slot); q->kind=0; q->slot=0; q->m=0; }
void usb_hotplug(void){ static u64 next; if(!ready||usb_hold||ticks<next)return; next=ticks+50; u64 f=irq_off(); int ch=0; for(int n=0;n<nports;n++){ u32 s=PS(n); int csc=s&(1<<17); if(csc)PS(n)=(s&0x0E00C200)|(1<<17); if((csc||!(s&1))&&(pt[n].kind||pt[n].slot)){ port_gone(n); ch=1; } if(csc||!(s&1))pt[n].tried=0; if((s&1)&&!pt[n].tried){ pt[n].tried=1; port_attach(n); ch=1; } } irq_on(f); if(ch)drives_sync(); }
void xhci_init(const pci_t*c){ if(cap)return; pci_enable(c); u64 bar=pci_bar(c,0); if(!bar)return; mmio_map(bar,0x10000); cap=bar; op=cap+(C32(0)&255); rt=cap+(C32(0x18)&~31u); db=cap+(C32(0x14)&~3u);
  u32 hc1=C32(0x10),hs1=C32(4),hs2=C32(8); csz=(hc1&4)?64:32; for(u32 x=hc1>>16,o=x*4;x;){ u64 a=cap+o; u32 v=*(volatile u32*)a; if((v&255)==1){ *(volatile u32*)a=v|(1<<24); for(int i=0;i<500000&&(*(volatile u32*)a&(1<<16));i++)udelay(2); } x=(v>>8)&255; o+=x*4; }
  O32(0)&=~1u; for(int i=0;i<200000&&!(O32(4)&1);i++)udelay(5); O32(0)|=2; for(int i=0;i<200000&&(O32(0)&2);i++)udelay(5); for(int i=0;i<200000&&(O32(4)&(1<<11));i++)udelay(5);
  int slots=hs1&255,ports=hs1>>24; if(ports>MAXP)ports=MAXP; nports=ports; if(slots>32)slots=32; O32(0x38)=slots; dcba=dma_alloc(2048); int sp=((hs2>>21)&31)<<5|(hs2>>27); if(sp){ u64*a=dma_alloc(sp*8); for(int i=0;i<sp;i++)a[i]=phys(dma_alloc(4096)); dcba[0]=phys(a); }
  O64(0x30)=phys(dcba); ring_new(&cmd,64); O64(0x18)=phys(cmd.t)|1;
  ev=dma_alloc(EVN*sizeof(trb)); u64*er=dma_alloc(16); er[0]=phys(ev); ((u32*)er)[2]=EVN; evc=1; R32(0x28)=1; R64(0x38)=phys(ev)|8; R64(0x30)=phys(er);
  O32(0)|=1; for(int i=0;i<200000&&(O32(4)&1);i++)udelay(5);
#define PKEEP 0x4F01C3E0u  /* Bits, die beim Schreiben erhalten bleiben; Aenderungsbits nicht */
  for(int n=0;n<ports;n++){ u32 s=PS(n); if(!(s&(1<<9))){ PS(n)=(s&0x0E00C200)|(1<<9); udelay(20000); s=PS(n); } if(!(s&1))continue; pt[n].tried=1; port_attach(n); }
  ready=1; }
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
static void kb_event(trb*e){ int s=e->d>>24; for(int i=0;i<usb_keyboards;i++) if(kb[i].slot==s){ kb_t*k=&kb[i]; u32 cc=e->c>>24; if(cc==1||cc==13)report(k,k->rep); u64 q=phys(k->rep); put_trb(&k->r,q,q>>32,8,(1<<10)|(1<<5)); DB(s)=k->dci; } }
void xhci_poll(void){ if(!ready)return; trb e; while(ev_next(&e)){ if(((e.d>>10)&63)==32)kb_event(&e); }
  if(rep_sc&&ticks>=rep_at){ sc(rep_sc,0); rep_at=ticks+3; } }
const char*usb_status(void){ return usb_keyboards?"USB-Tastatur":"keine"; }
