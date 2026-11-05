/* Eigenstaendige Programme: Pakete (.vpk), ELF-Lader, Adressraeume, Systemaufrufe.
   Alles aus einem Paket gilt als feindlich. Die Pruefung kommt ohne Kernel aus und ist auf dem Host testbar.
   Programme laufen in Ring 3 in der Adressraum-Scheibe PML4[200]. Der Kernel bleibt ihnen unzugaenglich,
   weil nur diese Scheibe das User-Bit traegt. Jedes Programm ist eine Koroutine: Es laeuft, bis es wartet,
   seine Zeitscheibe endet oder es einen Fehler macht. Ein Fehler beendet nur dieses Programm. */
#ifdef HOST
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#else
#include "os.h"
#endif
#define UBASE 0x640000000000ull
#define ULOAD (UBASE+0x400000ull)
#define UHEAP (UBASE+0x40000000ull)
#define USTACK (UBASE+0x7FF00000ull)
#define USTKSZ 0x100000ull
#define UEND  (USTACK+USTKSZ)
#define MAXCODE (64ull<<20)
#define MAXHEAP (64ull<<20)
#define VPK_MAX (32ull<<20)
#define VPK_HDR 256
typedef struct __attribute__((packed)) { char magic[4]; u32 elf; u16 cols,rows; char name[24],title[24],version[12],author[32],icon[3][7]; } vpk_h;
static int nulstr(const char*s,int n,int minlen){ int i=0; for(;i<n&&s[i];i++) if((u8)s[i]<32||s[i]==127)return 0; return i<n&&i>=minlen; }
static int vpk_check(const u8*b,u64 n){ if(n<VPK_HDR+64||n>VPK_MAX)return 0; const vpk_h*h=(const vpk_h*)b; if(memcmp(h->magic,"VPK1",4))return 0; if((u64)h->elf+VPK_HDR!=n)return 0; if(h->cols<20||h->cols>76||h->rows<5||h->rows>20)return 0; if(!nulstr(h->name,24,1)||!nulstr(h->title,24,1)||!nulstr(h->version,12,1)||!nulstr(h->author,32,0))return 0; for(int i=0;h->name[i];i++){ char c=h->name[i]; if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return 0; } for(int i=0;i<3;i++)if(!nulstr(h->icon[i],7,0))return 0; return 1; }
typedef struct { u64 off,vaddr,filesz,memsz; u32 flags; } seg_t;
static u64 r64(const u8*p){ u64 v=0; for(int i=7;i>=0;i--)v=v<<8|p[i]; return v; }
static u32 r32(const u8*p){ return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24; }
static u32 r16(const u8*p){ return (u32)p[0]|(u32)p[1]<<8; }
/* Liefert Einstieg und Segmente, oder 0 wenn das ELF abgelehnt wird */
static int elf_check(const u8*b,u64 n,u64*entry,seg_t*s,int*ns){ if(n<64||memcmp(b,"\x7f" "ELF\x02\x01\x01",7)||r16(b+16)!=2||r16(b+18)!=62)return 0; u64 ph=r64(b+32); u32 pes=r16(b+54),pn=r16(b+56); if(pes!=56||!pn||pn>16||ph>n||(u64)pn*56>n-ph)return 0; u64 e=r64(b+24); int c=0,xok=0; u64 last=0,total=0; for(u32 i=0;i<pn;i++){ const u8*p=b+ph+(u64)i*56; if(r32(p)!=1)continue; u32 fl=r32(p+4); u64 of=r64(p+8),va=r64(p+16),fs=r64(p+32),ms=r64(p+40); if(!ms)continue; if(c>=8||fs>ms||ms>MAXCODE||va<ULOAD||va>=UHEAP||va+ms>UHEAP||(va&4095)||of>n||fs>n-of||(fl&3)==3||(fl&~7u))return 0; if(va<last)return 0; last=(va+ms+4095)&~4095ull; total+=(ms+4095)&~4095ull; if(total>MAXCODE)return 0; if((fl&1)&&e>=va&&e<va+ms)xok=1; s[c].off=of; s[c].vaddr=va; s[c].filesz=fs; s[c].memsz=ms; s[c].flags=fl; c++; } if(!c||!xok)return 0; *entry=e; *ns=c; return 1; }
#ifndef HOST
/* ---------- Seiten ---------- */
#define PTE_P 1ull
#define PTE_W 2ull
#define PTE_U 4ull
#define PTE_NX (1ull<<63)
#define PA 0x000FFFFFFFFFF000ull
#define SLOT 200
static u64 pool_next,pool_end; static void*pg_free_list;
static void*pg_alloc(void){ void*p; if(pg_free_list){ p=pg_free_list; pg_free_list=*(void**)p; } else { if(pool_next>=pool_end){ if(pool_end)return 0; u64 f=heap_free()/2; if(f>(256ull<<20))f=256ull<<20; if(f<(16ull<<20))return 0; u8*m=kmalloc(f); if(!m)return 0; pool_next=((u64)m+4095)&~4095ull; pool_end=((u64)m+f)&~4095ull; } p=(void*)pool_next; pool_next+=4096; } memset(p,0,4096); return p; }
static void pg_put(void*p){ *(void**)p=pg_free_list; pg_free_list=p; }
#define V(e) ((u64*)(((e)&PA)+hhdm_off()))
static u64*walk(u64 root,u64 va,int make){ u64 e=root,*t; int sh[3]={30,21,12}; t=V(e); for(int l=0;l<2;l++){ u64*q=&t[(va>>sh[l])&511]; if(!(*q&PTE_P)){ if(!make)return 0; void*n=pg_alloc(); if(!n)return 0; *q=phys(n)|PTE_P|PTE_W|PTE_U; } t=V(*q); } return &t[(va>>12)&511]; }
static void*map_page(u64 root,u64 va,u64 fl){ u64*e=walk(root,va,1); if(!e)return 0; if(*e&PTE_P)return V(*e); void*p=pg_alloc(); if(!p)return 0; *e=phys(p)|PTE_P|PTE_U|fl; return p; }
static void as_free(u64 root){ u64*a=V(root); for(int i=0;i<512;i++) if(a[i]&PTE_P){ u64*b=V(a[i]); for(int j=0;j<512;j++) if(b[j]&PTE_P){ u64*c=V(b[j]); for(int k=0;k<512;k++) if(c[k]&PTE_P)pg_put(V(c[k])); pg_put(c); } pg_put(b); } pg_put(a); }
/* ---------- Programmzustand ---------- */
enum { P_RUN,P_WAIT,P_DONE,P_DEAD };
typedef struct { u64 root,brk,wake; regs r; u8 fxraw[528]; u8*fx; int state,kh,kt,dirty; u32 keys[32]; u16*cells; int cols,rows; char name[24],base[VFS_PATH],msg[70]; win_t*w; } proc_t;
static proc_t*cur; static u64 kctx,slice_end; static u8 ustack[262144] __attribute__((aligned(16)));
u64 proc_stack_top(void){ return ((u64)ustack+sizeof ustack)&~15ull; }
void proc_enter(regs*,u64*); void proc_back(u64);
extern u64*kpml4(void);
static void cr3_flush(void){ u64 c; __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3":"=r"(c)); }
static void leave_(regs*r){ proc_t*p=cur; p->r=*r; __asm__ volatile("fxsave64 (%0)"::"r"(p->fx):"memory"); proc_back(kctx); }
static void proc_run(proc_t*p){ if(cur)return; __asm__("cli"); cur=p; kpml4()[SLOT]=p->root|PTE_P|PTE_W|PTE_U; cr3_flush(); __asm__ volatile("fxrstor64 (%0)"::"r"(p->fx)); regs*f=(regs*)(ustack+sizeof ustack-sizeof(regs)-16); *f=p->r; slice_end=ticks+2; proc_enter(f,&kctx); kpml4()[SLOT]=0; cr3_flush(); cur=0; __asm__("sti"); }
void proc_tick(regs*r){ if(cur&&ticks>=slice_end)leave_(r); }
void proc_fault(regs*r){ proc_t*p=cur; p->state=P_DEAD; ksnprintf(p->msg,70,TR("Programmfehler %d bei %lx: beendet.","Program error %d at %lx: stopped."),(int)r->no,r->rip); leave_(r); }
/* Prueft, ob ein Bereich dem Programm gehoert. Danach darf der Kernel ihn direkt anfassen. */
static int uok(proc_t*p,u64 a,u64 n,int wr){ if(a<UBASE||a>=UEND||n>UEND-a)return 0; if(!n)return 1; for(u64 v=a&~4095ull;v<a+n;v+=4096){ u64*e=walk(p->root,v,0); if(!e||!(*e&PTE_P)||(wr&&!(*e&PTE_W)))return 0; } return 1; }
static int ustr(proc_t*p,u64 a,char*o,int max){ for(int i=0;i<max;i++){ if(!uok(p,a+i,1,0))return 0; o[i]=*(char*)(a+i); if(!o[i])return 1; } return 0; }
static int prefix(const char*p,const char*d){ size_t l=strlen(d); return strlen(p)>=l&&!memcmp(p,d,l)&&(!p[l]||p[l]=='/'); }
static int upath(proc_t*p,u64 a,char*o){ static char t[VFS_PATH]; if(!ustr(p,a,t,VFS_PATH))return 0; if(vfs_norm(p->base,t,o,VFS_PATH))return 0; return prefix(o,p->base)||prefix(o,"/home")||prefix(o,"/downloads")||prefix(o,"/drives"); }
typedef struct { u8*o; u64 n,max; } lc_t;
static int lcb(const vfs_ent*e,void*x){ lc_t*c=x; u64 l=strlen(e->name); if(c->n+l+2>c->max)return 1; memcpy(c->o+c->n,e->name,l); c->n+=l; if(e->dir)c->o[c->n++]='/'; c->o[c->n++]=0; return 0; }
static void pkey_add(proc_t*p,u32 v){ int n=(p->kt+1)%32; if(n!=p->kh){ p->keys[p->kt]=v; p->kt=n; } }
void proc_syscall(regs*r){ proc_t*p=cur; if(!p)return; u64 a=r->rdi,b=r->rsi,c=r->rdx; static char pa[VFS_PATH],pb[VFS_PATH]; long v=VE_DENIED; switch(r->rax){
 case 0: p->state=P_DONE; leave_(r); break;
 case 1: { u64 n=(u64)p->cols*p->rows*2; if(!uok(p,a,n,0)){ v=VE_INVAL; break; } memcpy(p->cells,(void*)a,n); v=0; break; }
 case 2: if(p->kh!=p->kt){ v=0; break; } r->rax=0; p->state=P_WAIT; p->wake=(long)a<0?~0ull:ticks+(a+9)/10; leave_(r); break;
 case 3: if(p->kh==p->kt)v=-1; else { v=p->keys[p->kh]; p->kh=(p->kh+1)%32; } break;
 case 4: v=(long)p->cols<<16|p->rows; break;
 case 5: { static char t[40]; if(ustr(p,a,t,40)){ win_title(p->w,t); v=0; } else v=VE_INVAL; break; }
 case 6: v=fs_now(); break;
 case 7: v=(long)(ticks*10); break;
 case 8: { u64 inc=(a+4095)&~4095ull; if(inc>MAXHEAP||p->brk-UHEAP+inc>MAXHEAP){ v=-1; break; } v=(long)p->brk; u64 e=p->brk+inc,o=p->brk; for(;o<e;o+=4096) if(!map_page(p->root,o,PTE_W|PTE_NX)){ v=-1; break; } p->brk=o; break; }
 case 9: v=key_down((u8)a); break;
 case 10: v=cfg_lang; break;
 case 11: if(!upath(p,a,pa)){ v=VE_DENIED; break; } if(!uok(p,b,c,1)){ v=VE_INVAL; break; } __asm__("sti"); v=fsx_read(pa,(void*)b,c); __asm__("cli"); break;
 case 12: if(!upath(p,a,pa)){ v=VE_DENIED; break; } if(!uok(p,b,c,0)){ v=VE_INVAL; break; } __asm__("sti"); v=fsx_write(pa,(void*)b,c,fs_now()); __asm__("cli"); break;
 case 13: { if(!upath(p,a,pa)){ v=VE_DENIED; break; } if(!uok(p,b,16,1)){ v=VE_INVAL; break; } vfs_ent e; __asm__("sti"); v=fsx_stat(pa,&e); __asm__("cli"); if(!v){ ((u64*)b)[0]=e.size; ((u64*)b)[1]=(u64)e.dir; } break; }
 case 14: if(!upath(p,a,pa)){ v=VE_DENIED; break; } __asm__("sti"); v=fsx_mkdir(pa,fs_now()); __asm__("cli"); break;
 case 15: if(!upath(p,a,pa)){ v=VE_DENIED; break; } if(!strcmp(pa,p->base)){ v=VE_DENIED; break; } __asm__("sti"); v=fsx_remove(pa); __asm__("cli"); break;
 case 16: if(!upath(p,a,pa)||!upath(p,b,pb)){ v=VE_DENIED; break; } if(!strcmp(pa,p->base)){ v=VE_DENIED; break; } __asm__("sti"); v=fsx_rename(pa,pb); __asm__("cli"); break;
 case 17: { if(!upath(p,a,pa)){ v=VE_DENIED; break; } if(!uok(p,b,c,1)){ v=VE_INVAL; break; } lc_t l={(u8*)b,0,c}; __asm__("sti"); v=fsx_list(pa,lcb,&l); __asm__("cli"); if(!v)v=(long)l.n; break; }
 default: v=VE_INVAL; }
 r->rax=(u64)v; }
/* ---------- Fenster ---------- */
static void pw_draw(win_t*w){ proc_t*p=w->st; cfill(w,AT(C_VOID,C_LAV)); for(int y=0;y<p->rows&&y<ch(w);y++) for(int x=0;x<p->cols&&x<cw(w);x++){ u16 c=p->cells[y*p->cols+x]; cput(w,x,y,(u8)c,(u8)(c>>8)); } if(p->state==P_DEAD){ ctext(w,1,0,AT(C_VOID,C_HOT),p->msg); ctext(w,1,1,AT(C_VOID,C_LAV),TR("Esc schließt das Fenster.","Esc closes the window.")); } }
static void pw_step(win_t*w){ proc_t*p=w->st; if(p->state==P_RUN||(p->state==P_WAIT&&(p->kh!=p->kt||ticks>=p->wake))){ p->state=P_RUN; proc_run(p); } if(p->state==P_DONE)win_close(w); }
static void pw_key(win_t*w,key_t k){ pkey_add(w->st,(u32)k.key|(u32)k.mods<<16); }
static void pw_free(win_t*w){ proc_t*p=w->st; as_free(p->root); kfree(p->cells); kfree(p); }
static const app_t pw_app={pw_draw,pw_key,pw_step,0,pw_free,pw_step};
static int load(const u8*b,u64 n,const char*base){ const vpk_h*h=(const vpk_h*)b; const u8*el=b+VPK_HDR; u64 en=h->elf,entry; seg_t s[8]; int ns; if(!elf_check(el,en,&entry,s,&ns))return VE_FORMAT; (void)n; proc_t*p=kmalloc(sizeof*p); if(!p)return VE_NOSPC; memset(p,0,sizeof*p); p->cells=kmalloc((u64)h->cols*h->rows*2); void*rt=pg_alloc(); if(!p->cells||!rt){ if(rt)pg_put(rt); kfree(p->cells); kfree(p); return VE_NOSPC; } p->root=phys(rt); p->cols=h->cols; p->rows=h->rows; for(int i=0;i<h->cols*h->rows;i++)p->cells[i]=(u16)(' '|AT(C_VOID,C_LAV)<<8);
  for(int i=0;i<ns;i++){ seg_t*q=&s[i]; u64 fl=((q->flags&2)?PTE_W:0)|((q->flags&1)?0:PTE_NX); for(u64 o=0;o<q->memsz;o+=4096){ u8*pg=map_page(p->root,q->vaddr+o,fl); if(!pg)goto oom; if(o<q->filesz){ u64 k=q->filesz-o; if(k>4096)k=4096; memcpy(pg,el+q->off+o,k); } } }
  for(u64 o=0;o<USTKSZ;o+=4096) if(!map_page(p->root,USTACK+o,PTE_W|PTE_NX))goto oom;
  p->brk=UHEAP; p->fx=(u8*)(((u64)p->fxraw+15)&~15ull); memset(p->fx,0,512); p->fx[0]=0x7F; p->fx[1]=3; p->fx[24]=0x80; p->fx[25]=0x1F; p->r.rip=entry; p->r.rsp=UEND-16; p->r.cs=0x23; p->r.ss=0x1B; p->r.rflags=0x202; strlcpy_(p->name,h->name,24); strlcpy_(p->base,base,VFS_PATH); p->state=P_RUN;
  p->w=win_open(h->title,(80-h->cols-2)/2,1+(21-h->rows)/2,h->cols+2,h->rows+2,&pw_app,p); if(!p->w)goto oom; return 0;
 oom: as_free(p->root); kfree(p->cells); kfree(p); return VE_NOSPC; }
/* Liest und prueft ein Paket */
static u8*pkg_read(const char*path,u64*n,int*err){ vfs_ent e; int r=fsx_stat(path,&e); if(r){ *err=r; return 0; } if(e.dir||e.size>VPK_MAX||e.size<VPK_HDR+64){ *err=VE_FORMAT; return 0; } u8*b=kmalloc(e.size+1); if(!b){ *err=VE_NOSPC; return 0; } int64_t l=fsx_read(path,b,e.size); if(l<0){ *err=(int)l; kfree(b); return 0; } u64 en; seg_t s[8]; int ns; if(!vpk_check(b,(u64)l)||!elf_check(b+VPK_HDR,(u64)l-VPK_HDR,&en,s,&ns)){ kfree(b); *err=VE_FORMAT; return 0; } *n=(u64)l; return b; }
int prog_run_file(const char*path,const char*base){ u64 n; int e=0; u8*b=pkg_read(path,&n,&e); if(!b)return e; e=load(b,n,base?base:fsx_home()); kfree(b); return e; }
static int safe_name(const char*s){ if(!*s||strlen(s)>23)return 0; for(;*s;s++) if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_'))return 0; return 1; }
/* Fest eingebaute Spiele: dieselben Pakete wie im SDK, nur schon im Kernel enthalten */
#include "apps.h"
static const struct { const char*name; const u8*d; u64 n; } builtin[]={{"snake",pkg_snake,sizeof pkg_snake},{"bricks",pkg_bricks,sizeof pkg_bricks},{"novastrike",pkg_novastrike,sizeof pkg_novastrike}};
static int is_builtin(const char*n){ for(unsigned i=0;i<sizeof builtin/sizeof builtin[0];i++) if(!strcmp(builtin[i].name,n))return 1; return 0; }
static void run_builtin(int i,const char*t){ char d[VFS_PATH]; ksnprintf(d,VFS_PATH,"/programs/%s/data",builtin[i].name); if(fs_ok()){ char a[VFS_PATH]; ksnprintf(a,VFS_PATH,"/programs/%s",builtin[i].name); fsx_mkdir("/programs",fs_now()); fsx_mkdir(a,fs_now()); fsx_mkdir(d,fs_now()); } int r=vpk_check(builtin[i].d,builtin[i].n)?load(builtin[i].d,builtin[i].n,d):VE_FORMAT; if(r)dlg_msg(t,vfs_errstr(r,cfg_lang)); }
void app_snake(void){ run_builtin(0,"Snake"); } void app_bricks(void){ run_builtin(1,"Bricks"); } void app_novastrike(void){ run_builtin(2,"Novastrike"); }
int prog_run(const char*name){ if(!safe_name(name))return VE_INVAL; char a[VFS_PATH],d[VFS_PATH]; ksnprintf(a,VFS_PATH,"/programs/%s/app.vpk",name); ksnprintf(d,VFS_PATH,"/programs/%s/data",name); fsx_mkdir(d,fs_now()); return prog_run_file(a,d); }
int prog_install(const char*path){ if(!fs_ok())return VE_NOFS; u64 n; int e=0; u8*b=pkg_read(path,&n,&e); if(!b)return e; const vpk_h*h=(const vpk_h*)b; char q[72],t[VFS_PATH],a[VFS_PATH]; ksnprintf(q,72,TR("%s %s installieren?","Install %s %s?"),h->title,h->version); ksnprintf(t,VFS_PATH,"/programs/%s",h->name); ksnprintf(a,VFS_PATH,"%s/app.vpk",t); vfs_ent v; const char*ti=fsx_stat(t,&v)?TR("Programm installieren","Install program"):TR("Programm ersetzen","Replace program"); if(!dlg_confirm(ti,q)){ kfree(b); return VE_DENIED; } fsx_mkdir("/programs",fs_now()); fsx_mkdir(t,fs_now()); e=fsx_write(a,b,n,fs_now()); kfree(b); return e; }
int prog_uninstall(const char*name){ if(!safe_name(name))return VE_INVAL; char t[VFS_PATH]; ksnprintf(t,VFS_PATH,"/programs/%s",name); return fsx_rmtree(t); }
int prog_scan(prog_info*o,int max){ if(!fs_ok())return 0; fs_item*it; int n=fs_load("/programs",&it),c=0; if(n<0){ kfree(it); return 0; } static u8 h[VPK_HDR]; for(int i=0;i<n&&c<max;i++){ if(!it[i].dir)continue; char a[VFS_PATH]; ksnprintf(a,VFS_PATH,"/programs/%s/app.vpk",it[i].name); if(fsx_read(a,h,VPK_HDR)!=VPK_HDR)continue; vpk_h*v=(vpk_h*)h; if(memcmp(v->magic,"VPK1",4)||!nulstr(v->name,24,1)||!nulstr(v->title,24,1)||strcmp(v->name,it[i].name)||is_builtin(v->name))continue; int ok=1; for(int k=0;k<3;k++)ok&=nulstr(v->icon[k],7,0); if(!ok)continue; memcpy(o[c].name,v->name,24); memcpy(o[c].title,v->title,24); memcpy(o[c].icon,v->icon,21); c++; } kfree(it); return c; }
#endif
