#include "os.h"
/* Textmodus-Desktop: Schattenpuffer, Fenster, Menueleiste, Dialoge. Reine Tastaturbedienung. */
#define MAXWIN 12
enum { A_DESK=AT(C_WALL,C_DESK),A_BAR=AT(C_RAISE,C_LAV),A_BARK=AT(C_RAISE,C_HOT),A_SEL=AT(C_SEL,C_GLOW),A_SELK=AT(C_SEL,C_HOT),A_FR=AT(C_PANEL,C_NEON),A_FRI=AT(C_PANEL,C_DIM),A_TIT=AT(C_PANEL,C_GLOW),A_DLG=AT(C_RAISE,C_LAV),A_DLGF=AT(C_RAISE,C_NEON),A_DLGT=AT(C_RAISE,C_GLOW),A_IN=AT(C_VOID,C_GLOW),A_SHD=AT(C_SHADOW,C_VOID) };
#define MAXW 240
#define MAXH 100
int scr_w=80,scr_h=25;
static u32 sb[MAXH][MAXW],pv[MAXH][MAXW];
static win_t wins[MAXWIN]; static win_t*z[MAXWIN]; static int nw;
static int cx=-1,cy=-1;

void put(int x,int y,u8 c,u8 a){ if(x>=0&&y>=0&&x<W&&y<H) sb[y][x]=(u32)c|(u32)(a&15)<<8|(u32)(a>>4)<<16; }
void put8(int x,int y,u8 c,u8 fg,u8 bg){ if(x>=0&&y>=0&&x<W&&y<H) sb[y][x]=(u32)c|(u32)fg<<8|(u32)bg<<16; }
static int u8dec(const char**s){ const u8*p=(const u8*)*s; u8 c=*p++; int r=c;
  if(c==0xC3){ u8 d=*p++; switch(d){case 0xA4:r=0x84;break;case 0xB6:r=0x94;break;case 0xBC:r=0x81;break;case 0x84:r=0x8E;break;case 0x96:r=0x99;break;case 0x9C:r=0x9A;break;case 0x9F:r=0xE1;break;default:r='?';} }
  else if(c==0xC2){ u8 d=*p++; r=d==0xA7?0x15:d==0xB0?0xF8:'?'; }
  *s=(const char*)p; return r; }
void text(int x,int y,u8 a,const char*s){ while(*s){ put(x++,y,u8dec(&s),a); } }
void fill(int x,int y,int w,int h,u8 c,u8 a){ for(int j=0;j<h;j++)for(int i=0;i<w;i++)put(x+i,y+j,c,a); }
/* Grafikausgabe: Zeichenraster auf dem Framebuffer, nur geaenderte Zellen */
#include "font.h"
static u8*fb; static u32 fbw,fbh,fbp,fbb,fbr,fbg,fbl; static int sc=1,ox,oy,force=1,pcx=-1,pcy=-1; static u32 col[256];
static u8 pal[16][3]; static int plevel=16;
static inline void px(u8*d,u32 c){ if(fbb==4)*(u32*)d=c; else memcpy(d,&c,fbb); }
/* Bildzellen: Bit 31 gesetzt, dann liegt in der Zelle kein Zeichen, sondern ein Ausschnitt von 4x8 Bildpunkten.
   So bleiben Fenster, Dialoge und Schatten wie gewohnt, nur die Zeichnung der Zelle unterscheidet sich. */
#define GN 32
static struct { u8 id; const u8*px; int stride; } gt[GN]; static int gn,gnext; static u8 gid;
u8 gfx_newid(void){ if(++gid==0||gid>=250)gid=1; return gid; }
static int gslot(u8 id,const u8*px,int stride){ for(int i=0;i<gn;i++) if(gt[i].id==id){ gt[i].px=px; gt[i].stride=stride; return 1; } int i; if(gn<GN)i=gn++; else { i=gnext; gnext=(gnext+1)%GN; } gt[i].id=id; gt[i].px=px; gt[i].stride=stride; return 1; }
static void gcell(int x,int y,u32 v){ u8 id=(u8)(v>>23); int ix=(v>>8)&255,iy=(v>>16)&127; const u8*p=0; int st=0; for(int i=0;i<gn;i++) if(gt[i].id==id){ p=gt[i].px; st=gt[i].stride; } int cw_=8*sc,ch_=16*sc,bs=2*sc; u32 c0=col[2]; for(int r=0;r<ch_;r++){ u8*q=fb+(oy+y*ch_+r)*fbp+(ox+x*cw_)*fbb; const u8*row=p?p+(u64)iy*8*st+(u64)(r/bs)*st+ix*4:0; for(int k=0;k<cw_;k++){ u32 c=row?col[row[k/bs]]:c0; px(q,c); q+=fbb; } } }
void gput(int x,int y,u8 id,const u8*px,int stride,int ix,int iy){ if(x>=0&&y>=0&&x<W&&y<H){ gslot(id,px,stride); sb[y][x]=0x80000020u|(u32)(ix&255)<<8|(u32)(iy&127)<<16|(u32)id<<23; } }
void cputg(win_t*w,int x,int y,u8 id,const u8*px,int stride,int ix,int iy){ if(x>=0&&y>=0&&x<cw(w)&&y<ch(w))gput(w->x+1+x,w->y+1+y,id,px,stride,ix,iy); }
static void cell(int x,int y,u32 v,int ul){ if(v>>31){ gcell(x,y,v); return; } const u8*g=font[v&0xFF]; u32 fg=col[(v>>8)&255],bg=col[(v>>16)&255];
  for(int r=0;r<16;r++){ u8 bits=(ul&&r>=14)?0xFF:g[r]; for(int s=0;s<sc;s++){ u8*d=fb+(oy+(y*16+r)*sc+s)*fbp+(ox+x*8*sc)*fbb;
      for(int c=0;c<8;c++){ u32 k=(bits&(0x80>>c))?fg:bg; for(int t=0;t<sc;t++){ px(d,k); d+=fbb; } } } } }
static void present(void){ for(int y=0;y<H;y++)for(int x=0;x<W;x++){ int cur=x==cx&&y==cy,was=x==pcx&&y==pcy; if(force||sb[y][x]!=pv[y][x]||cur!=was){ pv[y][x]=sb[y][x]; cell(x,y,sb[y][x],cur); } }
  pcx=cx;pcy=cy;force=0; }
void fb_init(void*a,u32 w,u32 h,u32 p,u32 bpp,u32 rs,u32 gs,u32 bs){ fb=a;fbw=w;fbh=h;fbp=p;fbb=bpp/8;fbr=rs;fbg=gs;fbl=bs; sc=w/640<h/400?w/640:h/400; if(sc<1)sc=1;
  W=w/(8*sc);H=h/(16*sc); if(W>MAXW)W=MAXW; if(H>MAXH)H=MAXH; ox=(w-W*8*sc)/2; oy=(h-H*16*sc)/2; memset(a,0,(u64)p*h); }
static void frame(int x,int y,int w,int h,int dbl,u8 a){ u8 tl=dbl?0xC9:0xDA,tr=dbl?0xBB:0xBF,bl=dbl?0xC8:0xC0,br=dbl?0xBC:0xD9,hz=dbl?0xCD:0xC4,vt=dbl?0xBA:0xB3;
  put(x,y,tl,a);put(x+w-1,y,tr,a);put(x,y+h-1,bl,a);put(x+w-1,y+h-1,br,a);
  for(int i=1;i<w-1;i++){put(x+i,y,hz,a);put(x+i,y+h-1,hz,a);} for(int i=1;i<h-1;i++){put(x,y+i,vt,a);put(x+w-1,y+i,vt,a);} }
static void shadow(int x,int y,int w,int h){ for(int i=0;i<h;i++)for(int k=0;k<2;k++){int X=x+w+k,Y=y+1+i; if(X<W&&Y<H)sb[Y][X]=(sb[Y][X]&0xFF)|(u32)(A_SHD&15)<<8|(u32)(A_SHD>>4)<<16;}
  for(int i=2;i<w+2;i++){int X=x+i,Y=y+h; if(X<W&&Y<H)sb[Y][X]=(sb[Y][X]&0xFF)|(u32)(A_SHD&15)<<8|(u32)(A_SHD>>4)<<16;} }

/* Zeichnen im Fensterinneren */
static win_t*clip;
int cw(win_t*w){return w->w-2;} int ch(win_t*w){return w->h-2;}
void cput(win_t*w,int x,int y,u8 c,u8 a){ if(x>=0&&y>=0&&x<cw(w)&&y<ch(w)) put(w->x+1+x,w->y+1+y,c,a); }
void ctext(win_t*w,int x,int y,u8 a,const char*s){ while(*s){ cput(w,x++,y,u8dec(&s),a);} }
void cput8(win_t*w,int x,int y,u8 c,u8 fg,u8 bg){ if(x>=0&&y>=0&&x<cw(w)&&y<ch(w)) put8(w->x+1+x,w->y+1+y,c,fg,bg); }
void cfill(win_t*w,u8 a){ fill(w->x+1,w->y+1,cw(w),ch(w),' ',a); }

/* Fenster */
win_t*win_top(void){ return nw?z[nw-1]:0; }
win_t*win_open(const char*t,int x,int y,int w,int h,const app_t*a,void*st){
  if(nw==MAXWIN){ dlg_msg(TR("Hinweis","Notice"),TR("Zu viele offene Fenster.","Too many open windows.")); return 0; }
  win_t*p=0; for(int i=0;i<MAXWIN;i++) if(!wins[i].app){p=&wins[i];break;}
  memset(p,0,sizeof*p); (void)x; (void)y; if(w>W)w=W; if(h>H-2)h=H-2; p->x=(W-w)/2;p->y=1+(H-2-h)/2;p->w=w;p->h=h;p->app=a;p->st=st; strlcpy_(p->title,t,40);
  if(p->x+w>W)p->x=W-w; if(p->y+h>H-1)p->y=H-1-h; z[nw++]=p; return p; }
void win_close(win_t*w){ if(w->app->can_close&&!w->app->can_close(w))return; if(w->app->destroy)w->app->destroy(w);
  int i=0; for(;i<nw;i++)if(z[i]==w)break; for(;i<nw-1;i++)z[i]=z[i+1]; nw--; w->app=0; }
void win_raise(win_t*w){ int i=0; for(;i<nw;i++)if(z[i]==w)break; if(i==nw)return; for(;i<nw-1;i++)z[i]=z[i+1]; z[nw-1]=w; }
void win_next(void){ if(nw<2)return; win_t*t=z[nw-1]; for(int i=nw-1;i>0;i--)z[i]=z[i-1]; z[0]=t; }
void win_zoom(win_t*w){ if(w->zoomed){w->x=w->sx;w->y=w->sy;w->w=w->sw;w->h=w->sh;w->zoomed=0;} else {w->sx=w->x;w->sy=w->y;w->sw=w->w;w->sh=w->h;w->x=0;w->y=1;w->w=W;w->h=H-2;w->zoomed=1;} }
void win_title(win_t*w,const char*t){ strlcpy_(w->title,t,40); }
static void draw_win(win_t*w,int active){ u8 a=active?A_FR:A_FRI;
  fill(w->x,w->y,w->w,w->h,' ',AT(C_PANEL,C_LAV)); w->cur=0; clip=w; w->app->draw(w);
  frame(w->x,w->y,w->w,w->h,active,a);
  char t[42]; ksnprintf(t,42," %s ",w->title); int l=0; for(const char*s=t;*s;l++)u8dec(&s);
  if(l>w->w-4)l=w->w-4; text(w->x+(w->w-l)/2,w->y,active?A_TIT:a,t);
  if(active&&w->foot[0]) text(w->x+2,w->y+w->h-1,a,w->foot);
  if(!w->zoomed) shadow(w->x,w->y,w->w,w->h);
  if(active&&w->cur){cx=w->x+1+w->cx;cy=w->y+1+w->cy;} }

/* Menue */
typedef struct { const char*de,*en; void(*fn)(void); const char*hkde,*hken; } mi;
#define MT(m) TR((m).de,(m).en)
#define MK(m) TR((m).hkde,(m).hken)
static void a_close(void){ if(win_top())win_close(win_top()); }
void win_place(win_t*w,int x,int y,int cols,int rows){ if(cols<0){ if(!w->zoomed)win_zoom(w); return; } if(w->zoomed)win_zoom(w); if(cols+2>W)cols=W-2; if(rows+2>H-2)rows=H-4; w->w=cols+2; w->h=rows+2; w->x=x<0?(W-w->w)/2:x; w->y=y<0?H-2-w->h:y; }
static void a_zoom(void){ if(win_top())win_zoom(win_top()); }
int ui_quitting; int ui_can_quit(void){ ui_quitting=1; for(int i=nw-1;i>=0;i--) if(z[i]->app->can_close&&!z[i]->app->can_close(z[i])){ ui_quitting=0; return 0; } ui_quitting=0; return 1; }
u32 ui_rgb(int i){ return (u32)pal[i][0]<<16|(u32)pal[i][1]<<8|pal[i][2]; }
static void a_reboot(void){ if(ui_can_quit()&&dlg_confirm(TR("Neustart","Restart"),TR("Violyx jetzt neu starten?","Restart Violyx now?")))reboot(); }
static void a_off(void){ if(ui_can_quit()&&dlg_confirm(TR("Ausschalten","Shut down"),TR("Violyx jetzt beenden?","Shut down Violyx now?")))poweroff(); }
static const mi m_sys[]={{"Über Violyx","About Violyx",dlg_about,0,0},{"Einstellungen","Settings",app_settings,0,0},{"Systeminfo","System info",app_info,0,0},{"Installieren","Install",app_install,0,0},{"Neustart","Restart",a_reboot,"Strg+Alt+Entf","Ctrl+Alt+Del"},{"Ausschalten","Shut down",a_off,0,0},{0,0,0,0,0}};
/* Programme-Menue: Uebersicht, darunter die offenen Fenster, oberstes zuerst */
static const mi m_app[]={{"Übersicht","Overview",app_launcher,"F2","F2"},{0,0,0,0,0}};
static mi dyn[MAXWIN+2]; static win_t*dynw[MAXWIN+1];
static const mi*app_items(void){ dyn[0]=m_app[0]; int n=1; for(int i=nw-1;i>=0;i--){ win_t*w=z[i]; dyn[n].de=dyn[n].en=w->title; dyn[n].fn=0; dyn[n].hkde=i==nw-1?"aktiv":0; dyn[n].hken=i==nw-1?"active":0; dynw[n]=w; n++; } memset(&dyn[n],0,sizeof dyn[n]); return dyn; }
static void menu_run(const mi*it,int i){ if(it[i].fn)it[i].fn(); else if(it==dyn)win_raise(dynw[i]); }
/* Schiebt das oberste Fenster an eine Ecke oder in die Mitte: 0 Mitte, 1 links oben, 2 rechts oben, 3 links unten, 4 rechts unten */
static void place(int c){ win_t*w=win_top(); if(!w)return; if(w->zoomed)win_zoom(w); int l=0,r=W-w->w,t=1,b=H-2-w->h;   /* eine Zeile frei fuer den Schatten, sonst verdeckt er die Leiste */ if(r<0)r=0; if(b<1)b=1; w->x=c==0?r/2:(c==1||c==3)?l:r; w->y=c==0?1+(H-2-w->h)/2:(c==1||c==2)?t:b; if(w->y<1)w->y=1; }
static void p_mid(void){ place(0); } static void p_lo(void){ place(1); } static void p_ro(void){ place(2); } static void p_lu(void){ place(3); } static void p_ru(void){ place(4); }
static const mi m_win[]={{"Zoomen","Zoom",a_zoom,"F5","F5"},{"Mittig","Centre",p_mid,0,0},{"Links oben","Top left",p_lo,0,0},{"Rechts oben","Top right",p_ro,0,0},{"Links unten","Bottom left",p_lu,0,0},{"Rechts unten","Bottom right",p_ru,0,0},{"Nächstes","Next",win_next,"F6","F6"},{"Schließen","Close",a_close,"Strg+W","Ctrl+W"},{0,0,0,0,0}};
static const mi m_hlp[]={{"Tastenübersicht","Keyboard help",app_help,"F1","F1"},{0,0,0,0,0}};
static const struct { const char*de,*en; const mi*it; } menus[]={{"System","System",m_sys},{"Programme","Programs",m_app},{"Fenster","Windows",m_win},{"Hilfe","Help",m_hlp}};
#define NM 4
static int mx(int i){ int x=2; for(int k=0;k<i;k++)x+=strlen(MT(menus[k]))+3; return x; }
static int mkey(int i){ char c=MT(menus[i])[0]; return c+32; }
static void bar(int open){ fill(0,0,W,1,' ',A_BAR); for(int i=0;i<NM;i++){ u8 n=i==open?A_SEL:A_BAR,k=i==open?A_SELK:A_BARK; int x=mx(i); const char*t=MT(menus[i]); put(x-1,0,' ',n); text(x,0,n,t); put(x,0,t[0],k); put(x+strlen(t),0,' ',n);} 
  rtc_t r; rtc_read(&r); char c[24]; if(cfg_lang)ksnprintf(c,24,"%d-%02d-%02d  %02d:%02d",r.year,r.mon,r.day,r.h,r.m); else ksnprintf(c,24,"%02d.%02d.%d  %02d:%02d",r.day,r.mon,r.year,r.h,r.m); text(W-19,0,A_BAR,c); }
static void status(void){ fill(0,H-1,W,1,' ',A_BAR); static const char*k[][3]={{"F1","Hilfe","Help"},{"F2","Programme","Programs"},{"F3","Dateien","Files"},{"F4","Befehle","Commands"},{"F10","Menü","Menu"},{"Esc","Zu","Close"}}; int x=1;
  for(unsigned i=0;i<6;i++){ text(x,H-1,A_BARK,k[i][0]); x+=strlen(k[i][0])+1; const char*t=k[i][cfg_lang?2:1]; text(x,H-1,A_BAR,t); int l=0; for(const char*p=t;*p;p++) if((*p&0xC0)!=0x80)l++; x+=l+2; } char v[24]; ksnprintf(v,24,"VIOLYX %s",VERSION); text(W-(int)strlen(v)-1,H-1,AT(C_RAISE,C_NEON),v); }
void ui_render(void){ cx=-1; if(cfg_wall>=6)wall_paint(); else fill(0,1,W,H-2,0xB1,A_DESK); bar(-1); status(); for(int i=0;i<nw;i++)draw_win(z[i],i==nw-1); }
key_t key_wait(void){ key_t k; while(!key_poll(&k)) idle(); return k; }
static void menu(int m){ int s=0;
  for(;;){ ui_render(); bar(m); const mi*it=m==1?app_items():menus[m].it; int n=0,wd=0; for(;it[n].de;n++){ int l=0; for(const char*p=MT(it[n]);*p;l++)u8dec(&p); if(it[n].hkde)l+=strlen(MK(it[n]))+2; if(l>wd)wd=l; }
    wd+=4; int x=mx(m)-1,y=1; if(x+wd>=W-2)x=W-wd-2; fill(x,y,wd,n+2,' ',A_DLG); frame(x,y,wd,n+2,0,A_DLG); shadow(x,y,wd,n+2);
    for(int i=0;i<n;i++){ u8 a=i==s?A_SEL:A_DLG; fill(x+1,y+1+i,wd-2,1,' ',a); text(x+2,y+1+i,a,MT(it[i])); if(it[i].hkde)text(x+wd-2-strlen(MK(it[i])),y+1+i,a,MK(it[i]));} 
    cx=-1; present(); key_t k=key_wait();
    switch(k.key){ case K_ESC:case K_F10:return; case K_UP:s=(s+n-1)%n;break; case K_DOWN:s=(s+1)%n;break;
      case K_LEFT:m=(m+NM-1)%NM;s=0;break; case K_RIGHT:m=(m+1)%NM;s=0;break; case K_ENTER: outb(0xE9,'m'); menu_run(it,s); return; 
      default: if(k.key<256){ int hits=0,first=-1,after=-1; for(int i=0;i<n;i++){ char c=MT(it[i])[0]; if(c>='A'&&c<='Z')c+=32; if(c==(char)k.key){ hits++; if(first<0)first=i; if(after<0&&i>s)after=i; } } if(hits==1){ outb(0xE9,'h'); menu_run(it,first); return; } if(hits>1)s=after>=0?after:first; } } } }

/* Dialoge */
void dlg_box(const char*t,int w,int h,int*x,int*y){ ui_render(); *x=(W-w)/2;*y=(H-h)/2; fill(*x,*y,w,h,' ',A_DLG); frame(*x,*y,w,h,1,A_DLGF); shadow(*x,*y,w,h); char b[44]; ksnprintf(b,44," %s ",t); text(*x+(w-(int)strlen(b))/2,*y,A_DLGT,b); }
void dlg_msg(const char*t,const char*s){ int x,y; int w=strlen(s)+6; if(w<30)w=30; if(w>76)w=76; dlg_box(t,w,7,&x,&y); text(x+3,y+2,A_DLG,s); text(x+w/2-6,y+4,A_SEL,TR(" [ Enter ] "," [ Enter ] ")); cx=-1; present(); for(;;){ key_t k=key_wait(); if(k.key==K_ENTER||k.key==K_ESC)return; } }
int dlg_confirm(const char*t,const char*s){ int x,y; int w=strlen(s)+6; if(w<34)w=34; if(w>76)w=76; dlg_box(t,w,7,&x,&y); text(x+3,y+2,A_DLG,s); text(x+w/2-11,y+4,A_SEL,TR(" J = Ja "," Y = Yes ")); text(x+w/2+2,y+4,A_SEL,TR(" N = Nein "," N = No ")); cx=-1; present();
  for(;;){ key_t k=key_wait(); if(k.key=='y'||k.key=='Y'||k.key=='j'||k.key=='J')return 1; if(k.key=='n'||k.key=='N'||k.key==K_ESC)return 0; } }
int dlg_fresh; int dlg_input(const char*t,const char*p,char*b,int max){ int n=strlen(b),fresh=dlg_fresh; dlg_fresh=0; for(;;){ int x,y; dlg_box(t,50,7,&x,&y); text(x+3,y+2,A_DLG,p); fill(x+3,y+3,44,1,' ',A_IN); int s=n>43?n-43:0; for(int i=s;i<n;i++)put(x+3+i-s,y+3,b[i],A_IN); text(x+3,y+5,A_DLG,TR("Enter = OK   Esc = Abbrechen","Enter = OK   Esc = Cancel")); cx=x+3+n-s;cy=y+3; present();
    key_t k=key_wait(); if(k.key==K_ENTER)return n>0; if(k.key==K_ESC)return 0; if(k.key==K_BS){ fresh=0; if(n)b[--n]=0; } else if(k.key>=32&&k.key<256&&!(k.mods&(M_CTRL|M_ALT))){ if(fresh){ n=0; b[0]=0; fresh=0; } if(n<max-1){ b[n++]=k.key;b[n]=0; } } } }
static int cmpi(const fs_item*a,const fs_item*b){ if(a->dir!=b->dir)return b->dir-a->dir; return strcmp(a->name,b->name); }
typedef struct { fs_item*it; int n,cap; } loadc;
static int loadcb(const vfs_ent*e,void*x){ loadc*l=x; if(l->n>=l->cap)return 1; fs_item*i=&l->it[l->n++]; strlcpy_(i->name,e->name,251); i->dir=e->dir; i->size=e->size; return 0; }
int fs_load(const char*p,fs_item**o){ loadc l={kmalloc(1024*sizeof(fs_item)),0,1024}; *o=l.it; if(!l.it)return -1; if(fsx_list(p,loadcb,&l)<0)return -1; for(int i=1;i<l.n;i++){ static fs_item t; memcpy(&t,&l.it[i],sizeof t); int k=i-1; while(k>=0&&cmpi(&l.it[k],&t)>0){ memcpy(&l.it[k+1],&l.it[k],sizeof t); k--; } memcpy(&l.it[k+1],&t,sizeof t); } return l.n; }
int dlg_pick(const char*t,char*o,int max){ static char cwd[VFS_PATH]; if(!cwd[0]||!fsx_quiet(cwd))strlcpy_(cwd,fsx_home(),VFS_PATH); int vis=12,s=0,top=0;
  for(;;){ fs_item*it; int n=fs_load(cwd,&it); if(n<0){ kfree(it); strlcpy_(cwd,"/",VFS_PATH); n=fs_load(cwd,&it); if(n<0){ kfree(it); return 0; } } if(s>=n)s=n?n-1:0;
    for(;;){ int x,y; dlg_box(t,60,vis+6,&x,&y); text(x+3,y+1,AT(C_RAISE,C_NEON),cwd); if(s<top)top=s; if(s>=top+vis)top=s-vis+1; for(int i=0;i<vis&&top+i<n;i++){ fs_item*e=&it[top+i]; char l[58]; if(e->dir)ksnprintf(l,58," [%s]",e->name); else ksnprintf(l,58," %s",e->name); u8 a=top+i==s?A_SEL:A_DLG; fill(x+2,y+2+i,56,1,' ',a); text(x+2,y+2+i,a,l); } if(!n)text(x+3,y+2,A_DLG,TR("(leer)","(empty)")); text(x+3,y+vis+3,A_DLG,TR("Enter Öffnen  Rücktaste Hoch  Esc Abbrechen","Enter Open  Backspace Up  Esc Cancel")); cx=-1; present();
      key_t k=key_wait(); if(k.key==K_UP&&s)s--; else if(k.key==K_DOWN&&s<n-1)s++; else if(k.key==K_ESC){ kfree(it); return 0; } else if(k.key==K_BS){ char p[VFS_PATH*2]; ksnprintf(p,sizeof p,"%s/..",cwd); vfs_norm("/",p,cwd,VFS_PATH); s=0;top=0; break; } else if(k.key==K_ENTER&&n){ char p[VFS_PATH+260]; ksnprintf(p,sizeof p,"%s/%s",strcmp(cwd,"/")?cwd:"",it[s].name); if(it[s].dir){ strlcpy_(cwd,p,VFS_PATH); s=0;top=0; break; } strlcpy_(o,p,max); kfree(it); return 1; } }
    kfree(it); } }
static const u8 pal0[16][3]={{0x08,0x06,0x0F},{0x1C,0x12,0x30},{0x16,0x0F,0x28},{0x2A,0x1C,0x4A},{0x4A,0x2C,0x8A},{0x6B,0x5B,0x95},{0x9A,0x7A,0xE0},{0xB8,0xA2,0xF0},
  {0x04,0x03,0x08},{0xA9,0x70,0xFF},{0xE0,0x40,0xFF},{0xDC,0xCC,0xFF},{0xFF,0x5C,0xC8},{0x08,0x06,0x0F},{0xC9,0xA0,0xFF},{0xF4,0xEC,0xFF}};
static void pal_set(int level){ plevel=level; for(int i=0;i<16;i++){ u32 r=pal[i][0]*level/16,g=pal[i][1]*level/16,b=pal[i][2]*level/16; col[i]=(r<<fbr)|(g<<fbg)|(b<<fbl); } static const u8 L[6]={0,95,135,175,215,255}; for(int i=16;i<256;i++){ u32 r,g,b; if(i<232){ int v=i-16; r=L[v/36]; g=L[(v/6)%6]; b=L[v%6]; } else r=g=b=8+10*(i-232); r=r*level/16; g=g*level/16; b=b*level/16; col[i]=(r<<fbr)|(g<<fbg)|(b<<fbl); } force=1; }
static const struct { const char*n,*en; u8 bg[3],ha[3]; } walls[6]={
  {"Nacht","Night",{0x08,0x06,0x0F},{0x1C,0x12,0x30}},{"Violett","Violet",{0x1E,0x0C,0x3C},{0x30,0x18,0x5C}},{"Mitternacht","Midnight",{0x06,0x0E,0x2C},{0x10,0x20,0x48}},
  {"Beere","Berry",{0x2A,0x08,0x24},{0x44,0x12,0x3A}},{"Petrol","Petrol",{0x04,0x20,0x28},{0x0A,0x34,0x3E}},{"Graphit","Graphite",{0x16,0x16,0x1A},{0x28,0x28,0x2E}}};
const char*wall_name(int i){ if(i==6)return "Logo"; if(i==7)return TR("Eigenes Bild","Own image"); return TR(walls[i].n,walls[i].en); }
void wall_set(int i){ cfg_wall=i; int k=i<6?i:0; memcpy(pal[C_WALL],walls[k].bg,3); memcpy(pal[C_DESK],walls[k].ha,3); pal_set(16); if(i>=6)wall_image(i); }
void dlg_about(void){ int x,y; dlg_box("Info",58,11,&x,&y); text(x+26,y+2,AT(C_RAISE,C_NEON),"Violyx"); char v[20]; ksnprintf(v,20,"Version %s",VERSION); text(x+(58-(int)strlen(v))/2,y+3,A_DLG,v); const char*a=TR("Ein 64-Bit-Betriebssystem für x86-PCs, nur per Tastatur,","A keyboard-only 64-bit operating system for x86 PCs,"),*b=TR("mit einem Retro-Textdesktop in leuchtendem Violett.","with a retro text desktop in glowing violet."); text(x+2,y+5,A_DLG,a); text(x+2,y+6,A_DLG,b); text(x+20,y+8,A_DLG,"(c) 2026 Merecete"); ui_show(); for(;;){ key_t k=key_wait(); if(k.key==K_ENTER||k.key==K_ESC)return; } }
int dlg_savechoice(void){ int x,y; dlg_box(TR("Nicht gespeichert","Not saved"),50,7,&x,&y); text(x+5,y+2,A_DLG,TR("Änderungen vor dem Schließen speichern?","Save changes before closing?")); text(x+3,y+4,A_SEL,TR(" J Speichern "," Y Save ")); text(x+18,y+4,A_SEL,TR(" N Verwerfen "," N Discard ")); text(x+34,y+4,A_SEL,TR(" Esc Zurück "," Esc Back ")); ui_show();
  for(;;){ key_t k=key_wait(); if(k.key=='s'||k.key=='S'||k.key=='j'||k.key=='J'||k.key=='y'||k.key=='Y')return 1; if(k.key=='n'||k.key=='N'||k.key==(u16)(cfg_lang?'d':'v')||k.key==(u16)(cfg_lang?'D':'V'))return 2; if(k.key==K_ESC)return 0; } }
void ui_init(void){ memcpy(pal,pal0,sizeof pal); wall_set(cfg_wall); }
void ui_goodbye(const char*a,const char*b){ memcpy(pal,pal0,sizeof pal); pal_set(16); fill(0,0,W,H,' ',AT(C_VOID,C_LAV)); text((W-(int)strlen(a))/2,H/2-1,AT(C_VOID,C_NEON),a); text((W-(int)strlen(b))/2,H/2+1,AT(C_VOID,C_LAV),b); cx=-1; present(); }
void ui_panic(const char*a,const char*b){ memcpy(pal,pal0,sizeof pal); pal_set(16); fill(0,0,W,H,' ',AT(C_HOT,C_VOID)); text(4,2,AT(C_HOT,C_VOID),a); if(b)text(4,4,AT(C_HOT,C_VOID),b); cx=-1; present(); }
void ui_show(void){ cx=-1; present(); }
void ui_splash(void){ static const char*l[]={
  "#   #  ###   ###   #     #   #  #   #","#   #   #   #   #  #      # #    # # ","#   #   #   #   #  #       #      #  "," # #    #   #   #  #       #     # # ","  #    ###   ###   #####   #    #   #"};
  static const u8 c[]={C_GLOW,C_LAV,C_NEON,C_VIOLET,C_SEL}; int lx=(W-74)/2,ly=(H-5)/2; fill(0,0,W,H,' ',AT(C_VOID,C_VOID));
  for(int y=0;y<5;y++) for(int x=0;l[y][x];x++) if(l[y][x]=='#'){ put(lx+x*2,ly+y,0xDB,AT(C_VOID,c[y])); put(lx+1+x*2,ly+y,0xDB,AT(C_VOID,c[y])); }
  cx=-1; pal_set(0); present();
  key_t k; for(int i=1;i<=16;i++){ pal_set(i); present(); u64 t=ticks+3; while(ticks<t) idle(); }
  u64 e=ticks+200; while(ticks<e&&!key_poll(&k)) idle(); while(key_poll(&k)); }
static int win_key(key_t k){ win_t*w=win_top(); if(!w)return 0; int c=k.mods&M_CTRL,s=k.mods&M_SHIFT;
  if(c&&!s&&(k.key==K_LEFT||k.key==K_RIGHT||k.key==K_UP||k.key==K_DOWN)&&!w->zoomed){ int dx=k.key==K_LEFT?-1:k.key==K_RIGHT?1:0,dy=k.key==K_UP?-1:k.key==K_DOWN?1:0; int nx=w->x+dx,ny=w->y+dy; if(nx>=0&&nx+w->w<=W)w->x=nx; if(ny>=1&&ny+w->h<=H-1)w->y=ny; return 1; }
  if(s&&!c&&(k.key==K_LEFT||k.key==K_RIGHT||k.key==K_UP||k.key==K_DOWN)&&!w->zoomed){ int nw_=w->w+(k.key==K_RIGHT)-(k.key==K_LEFT),nh=w->h+(k.key==K_DOWN)-(k.key==K_UP); if(nw_>=22&&w->x+nw_<=W)w->w=nw_; if(nh>=6&&w->y+nh<=H-1)w->h=nh; return 1; }
  return 0; }
void ui_run(void){ u64 last=0,nf=0; for(;;){ ui_render(); present(); key_t k; 
    while(!key_poll(&k)){ win_t*tw=win_top(); if(tw&&tw->app->frame&&ticks>=nf){ nf=ticks+4; tw->app->frame(tw); ui_render(); present(); } u64 t=ticks/100; if(t!=last){ last=t; for(int i=0;i<nw;i++) if(z[i]->app->tick) z[i]->app->tick(z[i]); ui_render(); present(); } idle(); }
    if(k.key==K_REBOOT){a_reboot();continue;}
    if(k.key==K_F10){menu(0);continue;}
    if(k.key==K_ESC){a_close();continue;}
    if(k.mods&M_ALT){ int done=0; for(int i=0;i<NM;i++) if(k.key==(u16)mkey(i)){menu(i);done=1;break;} if(done)continue; if(k.key==K_F3){a_close();continue;} }
    switch(k.key){ case K_F1:{ win_t*tw=win_top(); if(tw&&prog_wants(tw,K_F1))break; app_help(); continue; } case K_F2:app_launcher();continue; case K_F3:app_files();continue; case K_F4:app_shell();continue; case K_F5:a_zoom();continue; case K_F6:win_next();continue; case K_F7:app_web();continue; case K_F9:app_calc();continue; }
    if((k.mods&M_CTRL)&&k.key=='w'){a_close();continue;}
    if(win_key(k))continue;
    win_t*w=win_top(); if(w&&w->app->key)w->app->key(w,k); } }
