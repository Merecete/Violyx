#include "os.h"
#define T AT(C_PANEL,C_ORCH)
#define DIM AT(C_PANEL,C_MID)
#define HI AT(C_PANEL,C_NEON)
#define SEL AT(C_SEL,C_GLOW)
#define ST AT(C_VOID,C_VIOLET)
#define SI AT(C_VOID,C_NEON)
/* ---------- Schreiben ---------- */
typedef struct { char*b; int len,cur,top,col,dirty; char file[FS_NAME]; } ed_t;
#define ED_CAP (FS_MAXFILE-1)
static int ls(ed_t*e,int p){ while(p>0&&e->b[p-1]!='\n')p--; return p; }
static int le(ed_t*e,int p){ while(p<e->len&&e->b[p]!='\n')p++; return p; }
static int lineno(ed_t*e,int p){ int n=0; for(int i=0;i<p;i++) if(e->b[i]=='\n')n++; return n; }
static int linepos(ed_t*e,int n){ int p=0; while(n>0&&p<e->len){ if(e->b[p++]=='\n')n--; } return p; }
static void ed_name(win_t*w,ed_t*e){ char t[40]; ksnprintf(t,40,"%s%s",e->dirty?"* ":"",e->file[0]?e->file:TR("Unbenannt","Untitled")); win_title(w,t); }
static void ed_draw(win_t*w){ ed_t*e=w->st; int cl=lineno(e,e->cur),cc=e->cur-ls(e,e->cur),H_=ch(w),W_=cw(w);
  if(cl<e->top)e->top=cl; if(cl>=e->top+H_)e->top=cl-H_+1; if(cc<e->col)e->col=cc; if(cc>=e->col+W_)e->col=cc-W_+1;
  cfill(w,T); int p=linepos(e,e->top); for(int y=0;y<H_&&p<=e->len;y++){ int x=0; while(p<e->len&&e->b[p]!='\n'){ cput(w,x-e->col,y,(u8)e->b[p],T); x++;p++; } if(p>=e->len)break; p++; }
  w->cx=cc-e->col;w->cy=cl-e->top;w->cur=1; if(w->w>=52) ksnprintf(w->foot,64,TR(" Z %d  S %d   Strg+S Speichern  Strg+O Öffnen "," Ln %d  Col %d   Ctrl+S Save  Ctrl+O Open "),cl+1,cc+1); else ksnprintf(w->foot,64,TR(" Z %d  S %d "," Ln %d  Col %d "),cl+1,cc+1); ed_name(w,e); }
static void ed_save(ed_t*e){ if(!fs_ok()){ dlg_msg(TR("Speichern","Save"),fs_status()); return; } if(!e->file[0]&&!dlg_input(TR("Speichern unter","Save as"),TR("Dateiname:","File name:"),e->file,FS_NAME))return; if(fs_write(e->file,e->b,e->len)) dlg_msg(TR("Speichern","Save"),TR("Speichern fehlgeschlagen.","Saving failed.")); else e->dirty=0; }
static void ed_ins(ed_t*e,char c){ if(e->len>=ED_CAP)return; memmove(e->b+e->cur+1,e->b+e->cur,e->len-e->cur); e->b[e->cur++]=c; e->len++; e->dirty=1; }
static void ed_vert(ed_t*e,int d){ int c=e->cur-ls(e,e->cur),l=lineno(e,e->cur)+d; if(l<0)l=0; int p=linepos(e,l); int end=le(e,p); e->cur=p+c>end?end:p+c; }
static void ed_key(win_t*w,key_t k){ ed_t*e=w->st; int c=k.mods&M_CTRL;
  if(c){ if(k.key=='s')ed_save(e); else if(k.key=='o'){ char n[FS_NAME]; if(dlg_pick(TR("Öffnen","Open"),n,FS_NAME)) app_write(n); } else if(k.key==K_HOME)e->cur=0; else if(k.key==K_END)e->cur=e->len; return; }
  switch(k.key){ case K_LEFT:if(e->cur)e->cur--;break; case K_RIGHT:if(e->cur<e->len)e->cur++;break; case K_UP:ed_vert(e,-1);break; case K_DOWN:ed_vert(e,1);break;
   case K_HOME:e->cur=ls(e,e->cur);break; case K_END:e->cur=le(e,e->cur);break; case K_PGUP:ed_vert(e,-ch(w));break; case K_PGDN:ed_vert(e,ch(w));break;
   case K_ENTER:ed_ins(e,'\n');break; case K_TAB:for(int i=0;i<4;i++)ed_ins(e,' ');break;
   case K_BS:if(e->cur){ memmove(e->b+e->cur-1,e->b+e->cur,e->len-e->cur); e->cur--;e->len--;e->dirty=1; }break;
   case K_DEL:if(e->cur<e->len){ memmove(e->b+e->cur,e->b+e->cur+1,e->len-e->cur-1); e->len--;e->dirty=1; }break;
   default: if(k.key>=32&&k.key<256&&!(k.mods&M_ALT))ed_ins(e,k.key); } }
static int ed_cc(win_t*w){ ed_t*e=w->st; if(!e->dirty)return 1; int c=dlg_savechoice(); if(c==2)return 1; if(c==1){ ed_save(e); return !e->dirty; } return 0; }
static void ed_free(win_t*w){ ed_t*e=w->st; kfree(e->b); kfree(e); }
static const app_t ed_app={ed_draw,ed_key,0,ed_cc,ed_free};
void app_write(const char*f){ ed_t*e=kmalloc(sizeof*e); char*b=kmalloc(ED_CAP+1); if(!e||!b){ dlg_msg(TR("Schreiben","Write"),TR("Nicht genug Speicher.","Not enough memory.")); kfree(e);kfree(b); return; } memset(e,0,sizeof*e); e->b=b;
  if(f){ int n=fs_read(f,b,ED_CAP); if(n<0){ dlg_msg(TR("Öffnen","Open"),TR("Datei nicht lesbar.","File cannot be read.")); kfree(b);kfree(e); return; } e->len=n; strlcpy_(e->file,f,FS_NAME); }
  if(!win_open(TR("Unbenannt","Untitled"),6,3,62,17,&ed_app,e)){ kfree(b);kfree(e); } }
/* ---------- Dateien ---------- */
typedef struct { int s,top; } fm_t;
static int fm_idx(int*o){ int n=0; for(int i=0;fs_ok()&&i<FS_SLOTS;i++) if(fs_visible(i))o[n++]=i; return n; }
static void fm_draw(win_t*w){ win_title(w,TR("Dateien","Files")); fm_t*f=w->st; int o[FS_SLOTS],n=fm_idx(o); cfill(w,T); if(!fs_ok()){ ctext(w,1,1,T,TR("Datenträger:","Storage:")); ctext(w,1,2,HI,fs_status()); return; }
  if(f->s>=n)f->s=n?n-1:0; int vis=ch(w)-2; if(f->s<f->top)f->top=f->s; if(f->s>=f->top+vis)f->top=f->s-vis+1;
  ctext(w,1,0,HI,TR("Name                     Größe","Name                     Size"));
  for(int i=0;i<vis&&f->top+i<n;i++){ fs_entry*e=fs_slot(o[f->top+i]); char l[60]; ksnprintf(l,60,"%24s %d B",e->name,e->size); int sel=f->top+i==f->s; if(sel) for(int x=0;x<cw(w);x++)cput(w,x,i+1,' ',SEL); ctext(w,1,i+1,sel?SEL:T,l); }
  if(!n)ctext(w,1,2,DIM,TR("Noch keine Dateien. F2 startet Schreiben.","No files yet. Use F2 to open Write.")); ctext(w,1,ch(w)-1,DIM,TR("Enter Öffnen  N Neu  Entf Löschen","Enter Open  N New  Del Delete")); }
static void fm_key(win_t*w,key_t k){ fm_t*f=w->st; int o[FS_SLOTS],n=fm_idx(o);
  if(k.key==K_UP&&f->s)f->s--; else if(k.key==K_DOWN&&f->s<n-1)f->s++; else if(k.key=='n')app_write(0);
  else if(k.key==K_ENTER&&n)app_write(fs_slot(o[f->s])->name); else if(k.key==K_DEL&&n){ char t[60],nm[FS_NAME]; strlcpy_(nm,fs_slot(o[f->s])->name,FS_NAME); ksnprintf(t,60,TR("%s löschen?","Delete %s?"),nm); if(dlg_confirm(TR("Löschen","Delete"),t))fs_delete(nm); } }
static void simple_free(win_t*w){ kfree(w->st); }
static const app_t fm_app={fm_draw,fm_key,0,0,simple_free};
void app_files(void){ fm_t*f=kmalloc(sizeof*f); if(!f)return; memset(f,0,sizeof*f); if(!win_open(TR("Dateien","Files"),3,2,50,16,&fm_app,f))kfree(f); }
/* ---------- Befehle ---------- */
#define SL 120
#define SW 78
typedef struct { char l[SL][SW+1]; int n; char in[SW]; int il; int top; } sh_t;
static void sh_put(sh_t*s,const char*t){ if(s->n==SL){ memmove(s->l[0],s->l[1],(SL-1)*(SW+1)); s->n--; } strlcpy_(s->l[s->n++],t,SW+1); s->top=0; }
#define HOLE_MAX (1024*1024)
static sh_t*sh_cur; static void sh_prog(const char*t){ sh_put(sh_cur,t); ui_render(); ui_show(); }
static void sh_exec(sh_t*s){ char line[SW+4]; ksnprintf(line,SW+4,"> %s",s->in); sh_put(s,line); char*c=s->in; char*a=c; while(*a&&*a!=' ')a++; if(*a){*a++=0; while(*a==' ')a++;} char b[80];
  if(!*c){} else if(!strcmp(c,"hilfe")||!strcmp(c,"help")){ sh_put(s,TR("dir  type DATEI  del DATEI  edit DATEI  mem  zeit  ver  cls","dir  type FILE  del FILE  edit FILE  mem  time  ver  cls")); sh_put(s,TR("echo TEXT  netz  ping NAME  hole ADRESSE  web ADRESSE","echo TEXT  net  ping NAME  fetch ADDRESS  web ADDRESS")); sh_put(s,TR("neustart","restart")); }
  else if(!strcmp(c,"cls")||!strcmp(c,"clear")) s->n=0;
  else if(!strcmp(c,"ver")) sh_put(s,"Violyx " VERSION);
  else if(!strcmp(c,"echo")) sh_put(s,a);
  else if((!strcmp(c,"netz")||!strcmp(c,"net"))){ for(int i=0;i<6;i++){ net_info(b,80,i); if(b[0])sh_put(s,b); } }
  else if(!strcmp(c,"ping")){ if(!*a)sh_put(s,TR("Aufruf: ping NAME","Usage: ping NAME")); else { u32 ip; char t[16]; int r=net_resolve(a,&ip,t); if(r==-3)sh_put(s,net_status()); else if(r==-2)sh_put(s,TR("Abgebrochen.","Cancelled.")); else if(r)sh_put(s,TR("Name nicht gefunden.","Name not found.")); else { int rx=0,n=0; ksnprintf(b,80,TR("Ping an %s [%s]","Pinging %s [%s]"),a,t); sh_put(s,b); ui_render(); ui_show();
      for(int i=1;i<=4;i++){ int ms=0,ttl=0; r=net_ping(ip,i,&ms,&ttl); if(r==-2){ sh_put(s,TR("Abgebrochen.","Cancelled.")); break; } n++; if(r==0){ rx++; if(ms)ksnprintf(b,80,TR("Antwort von %s: Folge %d, Zeit %d ms, TTL %d","Reply from %s: seq %d, time %d ms, TTL %d"),t,i,ms,ttl); else ksnprintf(b,80,TR("Antwort von %s: Folge %d, Zeit <10 ms, TTL %d","Reply from %s: seq %d, time <10 ms, TTL %d"),t,i,ttl); } else ksnprintf(b,80,TR("Zeitüberschreitung (Folge %d)","Timed out (seq %d)"),i); sh_put(s,b); ui_render(); ui_show(); }
      ksnprintf(b,80,TR("%d gesendet, %d empfangen","%d sent, %d received"),n,rx); sh_put(s,b); } } }
  else if((!strcmp(c,"hole")||!strcmp(c,"fetch"))){ if(!*a)sh_put(s,TR("Aufruf: hole ADRESSE","Usage: fetch ADDRESS")); else { char*w=kmalloc(HOLE_MAX); http_res r; if(!w)sh_put(s,TR("Nicht genug Speicher.","Not enough memory.")); else { sh_cur=s; int n=http_get(a,0,w,HOLE_MAX,&r,sh_prog); if(n<0)sh_put(s,r.err); else { ksnprintf(b,80,"HTTP %d, %d bytes, %s%s",r.status,r.length,r.tls?TR("verschlüsselt, Zertifikat geprüft","encrypted, certificate verified"):TR("UNVERSCHLÜSSELT","NOT ENCRYPTED"),r.truncated?TR(", gekürzt",", truncated"):""); sh_put(s,b); if(r.tls&&tls_weak())sh_put(s,TR("Warnung: CPU ohne Zufallsgenerator, Verschlüsselung schwächer.","Warning: CPU has no random generator, encryption is weaker.")); if(r.type[0]){ ksnprintf(b,80,TR("Typ: %s","Type: %s"),r.type); sh_put(s,b); } char*p=w; for(int l=0;l<14&&*p;){ char*e=p; while(*e&&*e!='\n')e++; char sv=*e; *e=0; if(e>p&&e[-1]=='\r')e[-1]=0; for(char*q=p;*q;q++)if(*q=='\t')*q=' '; while(*p==' ')p++; if(*p){ sh_put(s,p); l++; } *e=sv; p=*e?e+1:e; } } kfree(w); } } }
  else if(!strcmp(c,"web")) web_app_open(*a?a:0);
  else if((!strcmp(c,"neustart")||!strcmp(c,"restart")||!strcmp(c,"reboot"))){ outb(0xE9,'S'); reboot(); }
  else if(!strcmp(c,"mem")){ ksnprintf(b,80,TR("RAM %ld KB, Heap frei %ld KB","RAM %ld KB, heap free %ld KB"),mem_total_kb,heap_free()/1024); sh_put(s,b); }
  else if((!strcmp(c,"zeit")||!strcmp(c,"time"))){ rtc_t r; rtc_read(&r); if(cfg_lang)ksnprintf(b,80,"%d-%02d-%02d %02d:%02d:%02d",r.year,r.mon,r.day,r.h,r.m,r.s); else ksnprintf(b,80,"%02d.%02d.%d %02d:%02d:%02d",r.day,r.mon,r.year,r.h,r.m,r.s); sh_put(s,b); }
  else if(!strcmp(c,"dir")||!strcmp(c,"ls")){ if(!fs_ok())sh_put(s,fs_status()); else { for(int i=0;i<FS_SLOTS;i++) if(fs_visible(i)){ ksnprintf(b,80,"%24s %d B",fs_slot(i)->name,fs_slot(i)->size); sh_put(s,b);} ksnprintf(b,80,TR("%d Datei(en)","%d file(s)"),fs_count()); sh_put(s,b);} }
  else if(!strcmp(c,"del")||!strcmp(c,"rm")) sh_put(s,fs_delete(a)?TR("Datei nicht gefunden.","File not found."):TR("Gelöscht.","Deleted."));
  else if(!strcmp(c,"edit")) app_write(*a?a:0);
  else if(!strcmp(c,"type")||!strcmp(c,"cat")){ static char f[4096]; int n=fs_read(a,f,4095); if(n<0)sh_put(s,TR("Datei nicht gefunden.","File not found.")); else { f[n]=0; char*p=f; while(p<f+n){ char*e=p; while(*e&&*e!='\n')e++; char sv=*e; *e=0; sh_put(s,p); *e=sv; p=e+1; } } }
  else sh_put(s,TR("Unbekannter Befehl. Tippe: hilfe","Unknown command. Type: help"));
  s->in[0]=0;s->il=0; }
static void sh_draw(win_t*w){ win_title(w,TR("Befehle","Commands")); sh_t*s=w->st; cfill(w,ST); int h=ch(w)-1,st=s->n-h-s->top; if(st<0)st=0; for(int i=0;i<h&&st+i<s->n;i++)ctext(w,0,i,ST,s->l[st+i]); int y=s->n-st; if(y>h)y=h; ctext(w,0,y,SI,"> "); int o=s->il>cw(w)-3?s->il-cw(w)+3:0; for(int i=o;i<s->il;i++)cput(w,2+i-o,y,s->in[i],AT(C_VOID,C_GLOW)); w->cx=2+s->il-o;w->cy=y;w->cur=1; }
static void sh_key(win_t*w,key_t k){ sh_t*s=w->st; if(k.key==K_ENTER)sh_exec(s); else if(k.key==K_BS){ if(s->il)s->in[--s->il]=0; } else if(k.key==K_PGUP){ if(s->top<s->n)s->top+=ch(w)-1; } else if(k.key==K_PGDN){ s->top-=ch(w)-1; if(s->top<0)s->top=0; } else if(k.key>=32&&k.key<256&&!(k.mods&(M_CTRL|M_ALT))&&s->il<SW-1){ s->in[s->il++]=k.key;s->in[s->il]=0; } }
static const app_t sh_app={sh_draw,sh_key,0,0,simple_free};
void app_shell(void){ sh_t*s=kmalloc(sizeof*s); if(!s)return; memset(s,0,sizeof*s); sh_put(s,TR("Violyx Befehle. Tippe: hilfe","Violyx commands. Type: help")); if(!win_open(TR("Befehle","Commands"),8,4,64,15,&sh_app,s))kfree(s); }
/* ---------- Rechner ---------- */
/* Festkomma: 4 Nachkommastellen, 64 Bit, bis rund 900 Billionen */
typedef long long i64;
#define SC 10000
#define I64MAX 0x7FFFFFFFFFFFFFFFLL
typedef struct { char in[32]; int n; char hist[8][72]; int hn; } calc_t;
static const char*cp; static int cerr;
static u64 udm(u64 n,u64 d,u64*r){ u64 q=0,m=0; for(int i=63;i>=0;i--){ m=(m<<1)|((n>>i)&1); if(m>=d){m-=d;q|=1ULL<<i;} } if(r)*r=m; return q; }
static i64 cadd(i64 a,i64 b){ i64 r; if(__builtin_add_overflow(a,b,&r)){cerr=3;return 0;} return r; }
static i64 cm(i64 a,i64 b){ i64 r; if(__builtin_mul_overflow(a,b,&r)){cerr=3;return 0;} return r; }
static u64 mag(i64 v){ return v<0?-(u64)v:(u64)v; }
static i64 cmul(i64 a,i64 b){ int neg=(a<0)!=(b<0); u64 af,bf,ai=udm(mag(a),SC,&af),bi=udm(mag(b),SC,&bf);
  i64 r=cm(cm((i64)ai,(i64)bi),SC); r=cadd(r,cm((i64)ai,(i64)bf)); r=cadd(r,cm((i64)af,(i64)bi)); r=cadd(r,(i64)udm(af*bf,SC,0)); return neg?-r:r; }
static i64 cdiv(i64 a,i64 b){ if(!b){cerr=2;return 0;} int neg=(a<0)!=(b<0); u64 m,q=udm(mag(a),mag(b),&m); i64 r=cm((i64)q,SC); int f=0;
  for(int i=0;i<4;i++){ if(m>0x1999999999999999ULL){cerr=3;return 0;} m*=10; u64 d=udm(m,mag(b),&m); f=f*10+(int)d; } r=cadd(r,f); return neg?-r:r; }
static i64 cmod(i64 a,i64 b){ if(!b){cerr=2;return 0;} u64 m; udm(mag(a),mag(b),&m); return a<0?-(i64)m:(i64)m; }
static i64 cexpr(void);
static void ws(void){ while(*cp==' ')cp++; }
static i64 catom(void){ ws(); if(*cp=='-'){cp++;return -catom();} if(*cp=='('){ cp++; i64 v=cexpr(); ws(); if(*cp==')')cp++; else cerr=1; return v; }
  if((*cp<'0'||*cp>'9')&&*cp!=','&&*cp!='.'){cerr=1;return 0;} i64 v=0; while(*cp>='0'&&*cp<='9'){ if(v>92233720368546LL){cerr=3;return 0;} v=v*10+*cp++-'0'; } v*=SC;
  if(*cp==','||*cp=='.'){ cp++; int f=SC/10; while(*cp>='0'&&*cp<='9'){ v+=f*(*cp++-'0'); f/=10; } } return v; }
static i64 cterm(void){ i64 v=catom(); for(;;){ ws(); char o=*cp; if(o=='x'||o=='X')o='*'; if(o==':')o='/'; if(o!='*'&&o!='/'&&o!='%')return v; cp++; i64 r=catom(); v=o=='*'?cmul(v,r):o=='/'?cdiv(v,r):cmod(v,r); if(cerr)return 0; } }
static i64 cexpr(void){ i64 v=cterm(); for(;;){ ws(); char o=*cp; if(o!='+'&&o!='-')return v; cp++; i64 r=cterm(); v=cadd(v,o=='+'?r:-r); if(cerr)return 0; } }
static void cfmt(i64 v,char*o){ u64 fr,ip=udm(mag(v),SC,&fr); char t[24]; int n=0,p=0; if(v<0)o[p++]='-'; do{ u64 r; ip=udm(ip,10,&r); t[n++]='0'+(int)r; }while(ip); while(n)o[p++]=t[--n];
  if(fr){ o[p++]=cfg_lang?'.':','; int f=(int)fr; int d=SC/10; while(f&&d){ o[p++]='0'+f/d; f%=d; d/=10; } } o[p]=0; }
static void ca_draw(win_t*w){ win_title(w,TR("Rechner","Calculator")); calc_t*c=w->st; cfill(w,T); for(int i=0;i<c->hn;i++)ctext(w,1,i,DIM,c->hist[i]); int y=ch(w)-1; ctext(w,1,y,HI,"= "); ctext(w,3,y,T,c->in); w->cx=3+c->n;w->cy=y;w->cur=1; }
static void ca_key(win_t*w,key_t k){ calc_t*c=w->st; if(k.key==K_BS){ if(c->n)c->in[--c->n]=0; }
  else if(k.key==K_ENTER&&c->n){ cp=c->in;cerr=0; i64 v=cexpr(); ws(); if(*cp&&!cerr)cerr=1; int max=ch(w)-1; if(max>8)max=8; while(c->hn>=max){ memmove(c->hist[0],c->hist[1],(c->hn-1)*72); c->hn--; } char r[32]; if(!cerr)cfmt(v,r); ksnprintf(c->hist[c->hn],72,"%s = %s",c->in,cerr==2?TR("Division durch 0","Division by 0"):cerr==3?TR("Zahl zu groß","Number too large"):cerr?TR("Eingabefehler","Input error"):r); c->hn++; c->in[0]=0;c->n=0; }
  else if(k.key>=32&&k.key<127&&c->n<31&&!(k.mods&(M_CTRL|M_ALT))){ c->in[c->n++]=k.key;c->in[c->n]=0; } }
static const app_t ca_app={ca_draw,ca_key,0,0,simple_free};
void app_calc(void){ calc_t*c=kmalloc(sizeof*c); if(!c)return; memset(c,0,sizeof*c); if(!win_open(TR("Rechner","Calculator"),36,3,42,12,&ca_app,c))kfree(c); }
/* ---------- Pong ---------- */
#define PH 4
#define WIN 7
typedef struct { int w,h,by,bx,vx,vy,py,cy,sp,sc,state,err; u32 rng; } pong_t;   /* Koordinaten 8.8-Festkomma */
static int prnd(pong_t*p,int n){ p->rng=p->rng*1103515245u+12345u; return (p->rng>>16)%n; }
static void serve(pong_t*p,int dir){ p->bx=(p->w/2)<<8; p->by=(1+prnd(p,p->h-2))<<8; p->vx=dir*0xB0; p->vy=(prnd(p,2)?1:-1)*(0x28+prnd(p,0x38)); p->err=prnd(p,5)-2; }
static void pong_reset(pong_t*p,win_t*w){ p->w=cw(w);p->h=ch(w); p->py=p->cy=((p->h+1-PH)/2)<<8; p->sp=p->sc=0; p->state=0; serve(p,1); }
static void bounce(pong_t*p,int pad,int dir){ int off=(p->by>>8)-(pad>>8)-PH/2; p->vy=off*0x38+prnd(p,0x18)-0x0C; int s=(p->vx<0?-p->vx:p->vx)+0x0C; if(s>0x1A0)s=0x1A0; p->vx=dir*s; }
static void pong_frame(win_t*w){ pong_t*p=w->st; if(p->w!=cw(w)||p->h!=ch(w))pong_reset(p,w); if(p->state!=1)return;
  int maxp=(p->h-PH)<<8;
  if(key_down(0x48)||key_down(0x11))p->py-=0xA0; if(key_down(0x50)||key_down(0x1F))p->py+=0xA0;
  if(p->py<0x100)p->py=0x100; if(p->py>maxp)p->py=maxp;
  int target=p->vx>0?(p->by-((PH/2)<<8)+(p->err<<8)):((p->h-PH)/2)<<8, spd=0x48+p->sp*2, d=target-p->cy;   /* mit Reaktionsgrenze, damit er schlagbar bleibt */
  p->cy+=d>spd?spd:d<-spd?-spd:d; if(p->cy<0x100)p->cy=0x100; if(p->cy>maxp)p->cy=maxp;
  int ox=p->bx; p->bx+=p->vx; p->by+=p->vy;
  if(p->by<0x100){p->by=0x200-p->by;p->vy=-p->vy;} if(p->by>=(p->h)<<8){p->by=(2*p->h<<8)-p->by-0x100;p->vy=-p->vy;}
  int lx=3<<8,rx=(p->w-4)<<8,row=p->by>>8;
  if(p->vx<0&&ox>=lx&&p->bx<lx&&row>=p->py>>8&&row<(p->py>>8)+PH){p->bx=lx;bounce(p,p->py,1);}
  else if(p->vx>0&&ox<=rx&&p->bx>rx&&row>=p->cy>>8&&row<(p->cy>>8)+PH){p->bx=rx;bounce(p,p->cy,-1);}
  if(p->bx<0){ p->sc++; if(p->sc>=WIN)p->state=2; else serve(p,1); } else if(p->bx>=p->w<<8){ p->sp++; if(p->sp>=WIN)p->state=2; else serve(p,-1); } }
static void pong_draw(win_t*w){ pong_t*p=w->st; if(p->w!=cw(w)||p->h!=ch(w))pong_reset(p,w); u8 bg=AT(C_VOID,C_DIM); cfill(w,bg); char s[40]; ksnprintf(s,40,TR("Du  %d : %d  Violyx","You  %d : %d  Violyx"),p->sp,p->sc); ctext(w,(p->w-(int)strlen(s))/2,0,AT(C_VOID,C_GLOW),s);
  for(int y=1;y<p->h;y+=2)cput(w,p->w/2,y,0xB3,bg);
  for(int i=0;i<PH;i++){ cput(w,2,(p->py>>8)+i,0xDB,AT(C_VOID,C_NEON)); cput(w,p->w-3,(p->cy>>8)+i,0xDB,AT(C_VOID,C_HOT)); }
  if(p->state==1||p->state==3)cput(w,p->bx>>8,p->by>>8,0xFE,AT(C_VOID,C_GLOW));
  const char*m=p->state==0?TR("Enter startet das Spiel","Press Enter to start"):p->state==3?TR("Pause - P geht weiter","Paused - press P"):p->state==2?(p->sp>=WIN?TR("Gewonnen! Enter = Neues Spiel","You win! Enter = New game"):TR("Violyx gewinnt. Enter = Neues Spiel","Violyx wins. Enter = New game")):0; if(m){ int l=0; for(const char*q=m;*q;q++)l++; ctext(w,(p->w-l)/2,p->h/2,AT(C_VOID,C_LAV),m); }
  strlcpy_(w->foot,TR(" Pfeile oben/unten   P Pause "," Up/Down arrows   P Pause "),64); }
static void pong_key(win_t*w,key_t k){ pong_t*p=w->st; if(k.key==K_ENTER){ if(p->state==0)p->state=1; else if(p->state==2){ pong_reset(p,w); p->state=1; } } else if(k.key=='p'||k.key=='P'){ if(p->state==1)p->state=3; else if(p->state==3)p->state=1; } }
static const app_t pong_app={pong_draw,pong_key,0,0,simple_free,pong_frame};
void app_pong(void){ pong_t*p=kmalloc(sizeof*p); if(!p)return; memset(p,0,sizeof*p); p->rng=(u32)ticks*2654435761u+1; win_t*w=win_open("Pong",7,2,66,20,&pong_app,p); if(!w){kfree(p);return;} pong_reset(p,w); }
/* ---------- Einstellungen ---------- */
typedef struct { int sel,dirty; rtc_t t; const char*msg; } set_t;
static int mdays(int m,int y){ static const u8 d[]={31,28,31,30,31,30,31,31,30,31,30,31}; return m==2&&y%4==0?29:d[m-1]; }
static void se_tick(win_t*w){ set_t*s=w->st; if(!s->dirty)rtc_read(&s->t); }
static void se_draw(win_t*w){ set_t*s=w->st; win_title(w,TR("Einstellungen","Settings")); static const char*lbd[]={"Tag","Monat","Jahr","Stunde","Minute","Hintergrund","Sprache","Tastatur"},*lbe[]={"Day","Month","Year","Hour","Minute","Background","Language","Keyboard"}; const char**lb=cfg_lang?lbe:lbd; int v[]={s->t.day,s->t.mon,s->t.year,s->t.h,s->t.m}; cfill(w,T); ctext(w,2,0,HI,TR("Datum und Uhrzeit","Date and time")); ctext(w,2,7,HI,TR("Darstellung","Appearance"));
  for(int i=0;i<8;i++){ int y=i<5?i+1:i+3; char b[50]; if(i<5)ksnprintf(b,50,"%12s  < %02d >",lb[i],v[i]); else ksnprintf(b,50,"%12s  < %s >",lb[i],i==5?wall_name(cfg_wall):i==6?(cfg_lang?"English":"Deutsch"):(cfg_kbd?"US":TR("Deutsch","German"))); int sel=s->sel==i; if(sel)for(int x=1;x<cw(w)-1;x++)cput(w,x,y,' ',SEL); ctext(w,2,y,sel?SEL:T,b); }
  ctext(w,2,12,s->msg?AT(C_PANEL,C_HOT):DIM,s->msg?s->msg:(s->dirty?TR("Enter übernimmt die Uhrzeit","Enter applies the time"):TR("Pfeile wählen und ändern","Arrows select and change"))); }
static void se_key(win_t*w,key_t k){ set_t*s=w->st; if(k.key==K_UP)s->sel=(s->sel+7)%8; else if(k.key==K_DOWN)s->sel=(s->sel+1)%8;
  else if(k.key==K_LEFT||k.key==K_RIGHT){ int d=k.key==K_RIGHT?1:-1; s->msg=0; if(s->sel>=5){ if(s->sel==5)wall_set((cfg_wall+WALLS+d)%WALLS); else if(s->sel==6)cfg_lang=!cfg_lang; else cfg_kbd=!cfg_kbd; if(cfg_save())s->msg=TR("Nur bis zum nächsten Start gültig","Valid until next restart only"); return; }
    rtc_t*t=&s->t; s->dirty=1; t->s=0; if(s->sel==0){ int m=mdays(t->mon,t->year); t->day=(t->day-1+m+d)%m+1; } else if(s->sel==1)t->mon=(t->mon-1+12+d)%12+1; else if(s->sel==2)t->year=2000+(t->year-2000+100+d)%100; else if(s->sel==3)t->h=(t->h+24+d)%24; else t->m=(t->m+60+d)%60;
    int m=mdays(t->mon,t->year); if(t->day>m)t->day=m; }
  else if(k.key==K_ENTER&&s->dirty){ rtc_set(&s->t); s->dirty=0; s->msg=cfg_save()?TR("Nur bis zum nächsten Start gültig","Valid until next restart only"):TR("Uhrzeit gespeichert","Time saved"); } }
static const app_t se_app={se_draw,se_key,se_tick,0,simple_free};
void app_settings(void){ set_t*s=kmalloc(sizeof*s); if(!s)return; memset(s,0,sizeof*s); rtc_read(&s->t); if(!win_open(TR("Einstellungen","Settings"),18,4,44,16,&se_app,s))kfree(s); }
/* ---------- Programmübersicht ---------- */
static void l_write(void){app_write(0);}
static const struct { const char*n,*en; void(*fn)(void); const char*ic[3]; u8 c; } progs[]={
 {"Schreiben","Write",l_write,{"\xDA\xC4\xC4\xC4\xC4\xBF","\xB3\xF0\xF0\xF0 \xB3","\xC0\xC4\xC4\xC4\xC4\xD9"},C_NEON},
 {"Internet","Internet",app_web,{"\xDA\xC4\xC2\xC2\xC4\xBF","\xC3\xC4\xC5\xC5\xC4\xB4","\xC0\xC4\xC1\xC1\xC4\xD9"},C_HOT},
 {"Dateien","Files",app_files,{"\xDC\xDC\xDC   ","\xDB\xDB\xDB\xDB\xDB\xDB","\xDB\xDB\xDB\xDB\xDB\xDB"},C_VIOLET},
 {"Befehle","Commands",app_shell,{"\xDA\xC4\xC4\xC4\xC4\xBF","\xB3>_  \xB3","\xC0\xC4\xC4\xC4\xC4\xD9"},C_ORCH},
 {"Rechner","Calculator",app_calc,{"\xDA\xC4\xC4\xC4\xC4\xBF","\xB3+-x:\xB3","\xC0\xC4\xC4\xC4\xC4\xD9"},C_HOT},
 {"Pong","Pong",app_pong,{"\xDB    \xDB","\xDB \xFE  \xDB","\xDB    \xDB"},C_NEON},
 {"Einstellungen","Settings",app_settings,{"\xC4\xFE\xC4\xC4\xC4\xC4","\xC4\xC4\xC4\xC4\xFE\xC4","\xC4\xC4\xFE\xC4\xC4\xC4"},C_VIOLET},
 {"Systeminfo","System info",app_info,{" \xDA\xC4\xC4\xBF ",  " \xB3i \xB3 "," \xC0\xC4\xC4\xD9 "},C_ORCH},
 {"Hilfe","Help",app_help,{" \xDA\xC4\xC4\xBF "," \xB3? \xB3 "," \xC0\xC4\xC4\xD9 "},C_HOT}
#ifdef LAUNCHER_TEST
 ,{"Hilfe","Help",app_help,{"  99  ","  99  ","  99  "},C_NEON},{"Hilfe","Help",app_help,{"  10  ","  10  ","  10  "},C_VIOLET},{"Hilfe","Help",app_help,{"  11  ","  11  ","  11  "},C_ORCH},{"Hilfe","Help",app_help,{"  12  ","  12  ","  12  "},C_HOT},{"Hilfe","Help",app_help,{"  13  ","  13  ","  13  "},C_NEON},{"Hilfe","Help",app_help,{"  14  ","  14  ","  14  "},C_VIOLET}
#endif
};
#define NP ((int)(sizeof progs/sizeof progs[0]))
#define PC 4
#define PV 2                                  /* sichtbare Reihen */
void app_launcher(void){ static int s,top; for(;;){ int x,y,rows=(NP+PC-1)/PC; dlg_box(TR("Programme","Programs"),66,17,&x,&y);
    if(s/PC<top)top=s/PC; if(s/PC>=top+PV)top=s/PC-PV+1;     /* Ansicht folgt der Auswahl */
    for(int i=top*PC;i<NP&&i<(top+PV)*PC;i++){ int tx=x+3+(i%PC)*15,ty=y+2+(i/PC-top)*7,sel=i==s; u8 bg=sel?C_SEL:C_RAISE; fill(tx,ty,15,6,' ',AT(bg,C_LAV));
      for(int r=0;r<3;r++) for(int c=0;c<6;c++) put(tx+4+c,ty+1+r,(u8)progs[i].ic[r][c],AT(bg,sel?C_GLOW:progs[i].c));
      const char*pn=TR(progs[i].n,progs[i].en); int l=strlen(pn); text(tx+(15-l)/2,ty+4,AT(bg,sel?C_GLOW:C_LAV),pn); }
    if(top>0)put(x+33,y+1,0x1E,AT(C_RAISE,C_HOT)); if(top+PV<rows)put(x+33,y+15,0x1F,AT(C_RAISE,C_HOT));
    ui_show(); key_t k=key_wait();
    if(k.key==K_ESC||k.key==K_F2)return; if(k.key==K_ENTER){ progs[s].fn(); return; }
    if(k.key==K_RIGHT){ if(s<NP-1)s++; } else if(k.key==K_LEFT){ if(s)s--; } else if(k.key==K_DOWN){ if(s/PC<rows-1){ s+=PC; if(s>=NP)s=NP-1; } } else if(k.key==K_UP){ if(s>=PC)s-=PC; } } }
/* ---------- Systeminfo ---------- */
static void in_draw(win_t*w){ char b[60]; rtc_t r; rtc_read(&r); u64 s=ticks/100; cfill(w,T); ctext(w,1,0,HI,"Violyx " VERSION); ksnprintf(b,60,TR("RAM:      %ld MB","RAM:      %ld MB"),mem_total_kb/1024); ctext(w,1,2,T,b); ksnprintf(b,60,TR("Heap:     %ld KB frei","Heap:     %ld KB free"),heap_free()/1024); ctext(w,1,3,T,b); ksnprintf(b,60,TR("Laufzeit: %d:%02d:%02d","Uptime:   %d:%02d:%02d"),(u32)(s/3600),(u32)((s/60)%60),(u32)(s%60)); ctext(w,1,4,T,b); ksnprintf(b,60,TR("Dateien:  %d / %d","Files:    %d / %d"),fs_count(),FS_SLOTS); ctext(w,1,5,T,b); ksnprintf(b,60,"Disk:     %s",fs_status()); ctext(w,1,6,T,b); ksnprintf(b,60,TR("USB-Tastaturen: %d","USB keyboards: %d"),usb_keyboards); ctext(w,1,7,T,b); ksnprintf(b,60,TR("Netzwerk: %s","Network:  %s"),net_status()); ctext(w,1,8,T,b); }
static const app_t in_app={in_draw,0,0,0,0};
void app_info(void){ win_open("Systeminfo",20,6,44,11,&in_app,0); }
/* ---------- Hilfe ---------- */
static const char*help_de[]={"Willkommen bei Violyx!","",
"F1 Hilfe    F2 Programme   F3 Dateien   F4 Befehle","F5 Zoom     F6 Fenster wechseln   F7 Internet   F9 Rechner","F10 Menü    Alt+S/P/F/H öffnet Menüs direkt","Esc schließt das Fenster (auch Strg+W oder Alt+F3)",
"Strg+Pfeile verschiebt, Umschalt+Pfeile ändert die Größe","","Im Schreibprogramm:","Strg+S speichern   Strg+O öffnen","Strg+Pos1/Ende springt an Anfang/Ende","","Sprache: System, Einstellungen, Sprache",0};
static const char*help_en[]={"Welcome to Violyx!","",
"F1 Help     F2 Programs    F3 Files     F4 Commands","F5 Zoom     F6 Switch   F7 Internet   F9 Calculator","F10 Menu    Alt+S/P/W/H opens menus directly","Esc closes the window (also Ctrl+W or Alt+F3)",
"Ctrl+Arrows moves, Shift+Arrows resizes","","In Write:","Ctrl+S save   Ctrl+O open","Ctrl+Home/End jumps to start/end","","Language: System, Settings, Language",0};
static void he_draw(win_t*w){ win_title(w,TR("Hilfe","Help")); cfill(w,T); const char**help=cfg_lang?help_en:help_de; for(int i=0;help[i];i++)ctext(w,1,i,i==0?HI:T,help[i]); }
static const app_t he_app={he_draw,0,0,0,0};
void app_help(void){ win_open(TR("Hilfe","Help"),9,4,62,16,&he_app,0); }
