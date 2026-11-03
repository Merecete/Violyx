#include "os.h"
/* Blockschicht, IDE-Treiber und VioFS.
   Sicherheitsregel: Es wird NIE automatisch formatiert. VioFS wird nur benutzt,
   wenn ein Datentraeger eine GPT-Partition vom Violyx-Typ UND die Signatur
   enthaelt. Alle anderen Datentraeger bleiben unberuehrt. */
#define MAXB 8
static blkdev bd[MAXB]; static int nb;
void blk_register(const char*n,u64 s,void*p,int(*r)(blkdev*,u64,void*),int(*w)(blkdev*,u64,const void*),int(*f)(blkdev*)){ if(nb<MAXB){ memset(&bd[nb],0,sizeof bd[nb]); bd[nb].name=n;bd[nb].sectors=s;bd[nb].p=p;bd[nb].rd=r;bd[nb].wr=w;bd[nb].fl=f; nb++; } }
void blk_model_ata(blkdev*d,const u16*w){ for(int i=0;i<20;i++){ d->model[i*2]=w[27+i]>>8; d->model[i*2+1]=w[27+i]&255; } d->model[40]=0; for(int i=39;i>=0&&d->model[i]==' ';i--)d->model[i]=0; }
int blk_count(void){return nb;} blkdev*blk_get(int i){return &bd[i];}
static blkdev*dev; static u64 FS_LBA;
#define FS_DATA (FS_LBA+8)
#define SLOT_SECT 128
typedef struct { char magic[8]; u32 ver,count; fs_entry e[FS_SLOTS]; u8 pad[2048-16-FS_SLOTS*32]; } fs_hdr;
_Static_assert(sizeof(fs_hdr)==2048,"hdr");
static fs_hdr hdr; static int ok; static const char*st="nicht initialisiert",*sten="not initialised";
#define SETST(a,b) (st=(a),sten=(b))
/* IDE: alle vier Positionen (zwei Kanaele mit je Master und Slave). CD-Laufwerke werden uebersprungen. */
typedef struct { u16 base,ctrl; u8 sl; u8 l48; } ide_t;
static ide_t ide[4]; static int nide;
static int wait_nbsy(u16 b){ for(u32 i=0;i<2000000;i++){ u8 s=inb(b+7); if(s==0xFF)return -1; if(!(s&0x80))return s; } return -1; }
static int wait_drq(u16 b){ for(u32 i=0;i<2000000;i++){ u8 s=inb(b+7); if(s==0xFF||(s&1))return -1; if(!(s&0x80)&&(s&8))return 0; } return -1; }
static void sel(ide_t*d,u8 hi){ outb(d->base+6,0xE0|(d->sl<<4)|hi); for(int i=0;i<4;i++)inb(d->ctrl); }
static void go(ide_t*d,u64 l,u8 c28,u8 c48){ u16 b=d->base; if(d->l48){ sel(d,0); outb(b+2,0); outb(b+3,l>>24); outb(b+4,l>>32); outb(b+5,l>>40); outb(b+2,1); outb(b+3,l); outb(b+4,l>>8); outb(b+5,l>>16); outb(b+7,c48); } else { sel(d,(l>>24)&15); outb(b+2,1); outb(b+3,l); outb(b+4,l>>8); outb(b+5,l>>16); outb(b+7,c28); } }
static int ide_rd(blkdev*x,u64 lba,void*b){ ide_t*d=x->p; sel(d,0); if(wait_nbsy(d->base)<0)return -1; go(d,lba,0x20,0x24); if(wait_drq(d->base))return -1; u16*p=b; for(int i=0;i<256;i++)p[i]=inw(d->base); return 0; }
static int ide_wr(blkdev*x,u64 lba,const void*b){ ide_t*d=x->p; sel(d,0); if(wait_nbsy(d->base)<0)return -1; go(d,lba,0x30,0x34); if(wait_drq(d->base))return -1; const u16*p=b; for(int i=0;i<256;i++)outw(d->base,p[i]); return wait_nbsy(d->base)<0?-1:0; }
static int ide_fl(blkdev*x){ ide_t*d=x->p; sel(d,0); if(wait_nbsy(d->base)>=0){ outb(d->base+7,d->l48?0xEA:0xE7); wait_nbsy(d->base); } return 0; }
static void ide_one(u16 base,u16 ctrl,u8 sl){ if(nide>=4)return; outb(ctrl,2); ide_t*d=&ide[nide]; d->base=base;d->ctrl=ctrl;d->sl=sl;d->l48=0; outb(base+6,0xA0|(sl<<4)); for(int i=0;i<15;i++)inb(ctrl); u8 s=inb(base+7); if(s==0xFF||s==0)return; for(u32 i=0;i<500000&&(inb(base+7)&0x80);i++); if(inb(base+4)==0x14&&inb(base+5)==0xEB)return; /* ATAPI */ outb(base+2,0);outb(base+3,0);outb(base+4,0);outb(base+5,0); outb(base+7,0xEC); for(int i=0;i<15;i++)inb(ctrl); if(!inb(base+7))return; if(inb(base+4)||inb(base+5))return; if(wait_drq(base))return; static u16 w[256]; for(int i=0;i<256;i++)w[i]=inw(base); u64 sec; if(w[83]&(1<<10)){ sec=*(u64*)(w+100); d->l48=1; } else sec=*(u32*)(w+60); if(!sec)return; nide++; blk_register("IDE",sec,d,ide_rd,ide_wr,ide_fl); blk_model_ata(blk_get(nb-1),w); }
void ide_probe(void){ ide_one(0x1F0,0x3F6,0); ide_one(0x1F0,0x3F6,1); ide_one(0x170,0x376,0); ide_one(0x170,0x376,1); }
static int rd(u64 lba,void*b){ return dev->rd(dev,lba,b); }
static int wr(u64 lba,const void*b){ return dev->wr(dev,lba,b); }
static int save_hdr(void){ for(int i=0;i<4;i++) if(wr(FS_LBA+i,(u8*)&hdr+i*512))return -1; dev->fl(dev); return 0; }
static const char GUID[16]="VIOLYXFSPARTITN1";
static int probe(blkdev*d){ static u8 s[512]; if(d->rd(d,1,s)||memcmp(s,"EFI PART",8))return 0; u64 el=*(u64*)(s+72); u32 cnt=*(u32*)(s+80),esz=*(u32*)(s+84); if(esz<128||esz>512||cnt>512)return 0;
  for(u32 i=0;i<cnt;i++){ u64 sec=el+i*esz/512; if(d->rd(d,sec,s))return 0; u8*e=s+(i*esz)%512; if(memcmp(e,GUID,16))continue; u64 lba=*(u64*)(e+32); static u8 h[512]; if(d->rd(d,lba,h)||memcmp(h,"VIOLYXFS",8))return 0; dev=d; FS_LBA=lba; return 1; }
  return 0; }
static char stbuf[48];
int fs_attach(blkdev*d){ if(!probe(d))return -1; for(int k=0;k<4;k++) if(rd(FS_LBA+k,(u8*)&hdr+k*512))return -1; ok=1; return 0; }
int fs_init(void){ if(!nb){ SETST("kein beschreibbarer Datenträger","no writable storage"); return -1; }
  for(int i=0;i<nb;i++) if(probe(&bd[i])){ for(int k=0;k<4;k++) if(rd(FS_LBA+k,(u8*)&hdr+k*512)){ SETST("Lesefehler","read error"); return -1; } ok=1;  return 0; }
  SETST("kein VioFS (Medien unberührt)","no VioFS (media untouched)"); return -1; }
blkdev*fs_device(void){ return ok?dev:0; }
int fs_ok(void){return ok;} const char*fs_status(void){ if(ok){ ksnprintf(stbuf,48,TR("bereit (%s)","ready (%s)"),dev->name); return stbuf; } return TR(st,sten); }
fs_entry*fs_slot(int i){ return &hdr.e[i]; }
int fs_count(void){ int n=0; if(ok) for(int i=0;i<FS_SLOTS;i++) if(fs_visible(i))n++; return n; }
int fs_visible(int i){ return hdr.e[i].used&&hdr.e[i].name[0]!='.'; }
int fs_find(const char*n){ if(!ok)return -1; for(int i=0;i<FS_SLOTS;i++) if(hdr.e[i].used&&!strcmp(hdr.e[i].name,n))return i; return -1; }
int fs_read(const char*n,char*b,int max){ int i=fs_find(n); if(i<0)return -1; int sz=hdr.e[i].size; if(sz>max)sz=max;
  static u8 s[512]; for(int o=0,k=0;o<sz;o+=512,k++){ if(rd(FS_DATA+i*SLOT_SECT+k,s))return -1; int c=sz-o>512?512:sz-o; memcpy(b+o,s,c); } return sz; }
int fs_write(const char*n,const char*b,int len){ if(!ok||len>FS_MAXFILE||!n[0]||strlen(n)>=FS_NAME)return -1; int i=fs_find(n);
  if(i<0){ for(i=0;i<FS_SLOTS&&hdr.e[i].used;i++); if(i==FS_SLOTS)return -1; }
  static u8 s[512]; for(int o=0,k=0;o<len;o+=512,k++){ int c=len-o>512?512:len-o; memset(s,0,512); memcpy(s,b+o,c); if(wr(FS_DATA+i*SLOT_SECT+k,s))return -1; }
  strlcpy_(hdr.e[i].name,n,FS_NAME); hdr.e[i].size=len; hdr.e[i].used=1; return save_hdr(); }
int fs_delete(const char*n){ int i=fs_find(n); if(i<0)return -1; hdr.e[i].used=0; return save_hdr(); }
