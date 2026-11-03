#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "viofs.h"
#define VERSION "0.1.0"
extern int cfg_lang,cfg_kbd;
#define TR(de,en) (cfg_lang?(en):(de))
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;

/* Portzugriff */
static inline void outb(u16 p, u8 v){ __asm__ volatile("outb %0,%1":: "a"(v),"Nd"(p)); }
static inline u8 inb(u16 p){ u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u16 inw(u16 p){ u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outw(u16 p,u16 v){ __asm__ volatile("outw %0,%1":: "a"(v),"Nd"(p)); }
static inline void outl(u16 p,u32 v){ __asm__ volatile("outl %0,%1":: "a"(v),"Nd"(p)); }
static inline u32 inl(u16 p){ u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void udelay(u32 n){ while(n--) inb(0x80); }

/* lib.c */
void *memset(void*,int,size_t); void *memcpy(void*,const void*,size_t);
void *memmove(void*,const void*,size_t); int memcmp(const void*,const void*,size_t);
size_t strlen(const char*); int strcmp(const char*,const char*);
void strlcpy_(char*,const char*,size_t);
int ksnprintf(char*,size_t,const char*,...);
void heap_init(u64 start,u64 end); void *kmalloc(size_t); void kfree(void*);
u64 heap_free(void); void *dma_alloc(size_t); u64 phys(const void*); void mmio_map(u64,u64);

/* main.c */
extern volatile u64 ticks; extern u64 mem_total_kb; void idle(void);
void reboot(void); void poweroff(void); void acpi_poweroff(u64 rsdp); void panic(const char*);
typedef struct { u8 h,m,s,day,mon; u16 year; } rtc_t;
void rtc_read(rtc_t*); void rtc_set(const rtc_t*); u32 rtc_unix(const rtc_t*); void rtc_from_unix(u32,rtc_t*);
extern int cfg_wall; int cfg_save(void); void cfg_load(void);

/* kbd.c */
enum { K_F1=0x100,K_F2,K_F3,K_F4,K_F5,K_F6,K_F7,K_F8,K_F9,K_F10,K_F11,K_F12,
       K_UP,K_DOWN,K_LEFT,K_RIGHT,K_HOME,K_END,K_PGUP,K_PGDN,K_INS,K_DEL,
       K_ENTER,K_ESC,K_BS,K_TAB,K_REBOOT };
#define M_SHIFT 1
#define M_CTRL  2
#define M_ALT   4
typedef struct { u16 key; u8 mods; } key_t;
void kbd_init(void); void kbd_irq(void); void kbd_in(u8 scancode); int key_poll(key_t*); int key_down(u8 scancode);

/* disk.c: ATA und VioFS */
typedef struct blkdev { const char *name; u64 sectors; void *p; char model[41]; int usb,gone; u32 id;
  int(*rd)(struct blkdev*,u64,void*); int(*wr)(struct blkdev*,u64,const void*); int(*fl)(struct blkdev*); } blkdev;
blkdev *blk_register(const char*name,u64 sectors,void*p,int(*rd)(blkdev*,u64,void*),int(*wr)(blkdev*,u64,const void*),int(*fl)(blkdev*));
int blk_count(void); blkdev *blk_get(int); void ide_probe(void); void blk_gone(blkdev*); blkdev *fs_device(void); int fs_attach(blkdev*); void blk_model_ata(blkdev*,const u16*);
void install_set_image(const void*,u64); int install_available(void); void app_install(void);
/* pci.c und Treiber */
typedef struct { u8 bus,dev,fn,cls,sub,pif; u16 ven,did; } pci_t;
int pci_scan(pci_t*,int max); u32 pci_rd(const pci_t*,u8 off); void pci_wr(const pci_t*,u8 off,u32 v);
u64 pci_bar(const pci_t*,int n); void pci_enable(const pci_t*);
extern int usb_hold; void usb_hotplug(void); void ahci_init(const pci_t*); void nvme_init(const pci_t*); void xhci_init(const pci_t*); void xhci_poll(void);
void e1000_init(const pci_t*); void net_init(void); void net_poll(void); int net_present(void); int net_ready(void); const char *net_status(void); void net_info(char*,int,int);
extern int net_aborted; int tcp_open(u32 ip,u16 port); int tcp_tx(const void*,size_t); int tcp_tx_all(const void*,int); int tcp_rx(void*,size_t); void tcp_end(void);
extern char tls_err[80]; int tls_open(const char*host); int tls_write(const void*,int); int tls_read(void*,int); void tls_close(void); int tls_weak(void);
typedef struct { int status,length,tls,redirects,truncated,cert_bad; char type[60],err[80],final_host[120],final_url[1200]; } http_res;
int http_get(const char*url,const char*post,char*buf,int max,http_res*r,void(*progress)(const char*));
int atoi(const char*); int strncmp(const char*,const char*,size_t); char *strstr(const char*,const char*); char *strchr(const char*,int);
int net_resolve(const char*,u32*ip,char*txt); int net_ping(u32 ip,u16 seq,int*ms,int*ttl);
extern int usb_keyboards; const char *usb_status(void);
int fs_init(void); int fs_ok(void); const char *fs_status(void); u32 fs_now(void); void fs_detach(void); int fs_live(void);

/* drives.c: VioFS und USB-Laufwerke unter /drives */
extern u32 fsx_gen; void drives_sync(void); void drives_reset(void);
int fsx_stat(const char*,vfs_ent*); int64_t fsx_read(const char*,void*,u64); int fsx_write(const char*,const void*,u64,u32); int fsx_mkdir(const char*,u32);
int fsx_remove(const char*); int fsx_rename(const char*,const char*); int fsx_list(const char*,int(*)(const vfs_ent*,void*),void*);
int fsx_protected(const char*); int fsx_rmtree(const char*); int fsx_copy(const char*,const char*); int fsx_quiet(const char*); int fsx_first(char*,int); const char *fsx_home(void); void fsx_info(const char*,char*,int); int fsx_eject(const char*);

/* Violyx-Palette: 16 Farben, in ui_init() in die VGA-Hardware geschrieben */
enum { C_VOID,C_DESK,C_PANEL,C_RAISE,C_SEL,C_DIM,C_MID,C_SOFT,C_SHADOW,C_VIOLET,C_NEON,C_LAV,C_HOT,C_WALL,C_ORCH,C_GLOW };
#define AT(b,f) (((b)<<4)|(f))

/* ui.c */
extern int scr_w,scr_h;
#define W scr_w
#define H scr_h
struct win;
typedef struct { void(*draw)(struct win*); void(*key)(struct win*,key_t); 
                 void(*tick)(struct win*); int(*can_close)(struct win*); void(*destroy)(struct win*); void(*frame)(struct win*); } app_t;
typedef struct win { int x,y,w,h, sx,sy,sw,sh, zoomed; char title[40]; const app_t *app; void *st;
                     int cx,cy,cur; char foot[64]; } win_t;
void put(int x,int y,u8 ch,u8 attr);
void text(int x,int y,u8 attr,const char*s);
void fill(int x,int y,int w,int h,u8 ch,u8 attr);
void cput(win_t*,int x,int y,u8 ch,u8 attr);      /* im Fensterinneren, mit Clipping */
void ctext(win_t*,int x,int y,u8 attr,const char*);
void cfill(win_t*,u8 attr);
int cw(win_t*); int ch(win_t*);
win_t *win_open(const char*t,int x,int y,int w,int h,const app_t*,void*st);
void win_close(win_t*); void win_next(void); void win_zoom(win_t*); win_t *win_top(void); int ui_can_quit(void);
void win_title(win_t*,const char*);
void fb_init(void*addr,u32 w,u32 h,u32 pitch,u32 bpp,u32 rs,u32 gs,u32 bs); void ui_panic(const char*a,const char*b);
void ui_goodbye(const char*,const char*); void ui_init(void); void ui_splash(void); void ui_show(void); key_t key_wait(void);
#define WALLS 6
void wall_set(int); const char *wall_name(int);
void dlg_box(const char*,int w,int h,int*x,int*y); void dlg_about(void); int dlg_savechoice(void); void ui_run(void); void ui_render(void);
void dlg_msg(const char*title,const char*text);
int  dlg_confirm(const char*title,const char*text);
extern int dlg_fresh; int  dlg_input(const char*title,const char*prompt,char*buf,int max);
int  dlg_pick(const char*title,char*out,int max);
typedef struct { char name[251]; int dir; u64 size; } fs_item;
int fs_load(const char*path,fs_item**out);

/* apps.c */
void app_write(const char*file); void app_files(void); void app_shell(void);
void app_calc(void); void app_info(void); void app_help(void);
void app_launcher(void); void app_pong(void); void app_settings(void); void app_web(void); void web_app_open(const char*);
