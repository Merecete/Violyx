#include "os.h"
#include "limine.h"
#define REQ __attribute__((used,section(".limine_requests"))) static volatile
REQ LIMINE_BASE_REVISION(3);
REQ struct limine_framebuffer_request fbreq={.id=LIMINE_FRAMEBUFFER_REQUEST,.revision=0};
REQ struct limine_memmap_request mmreq={.id=LIMINE_MEMMAP_REQUEST,.revision=0};
REQ struct limine_hhdm_request hhreq={.id=LIMINE_HHDM_REQUEST,.revision=0};
REQ struct limine_rsdp_request rsreq={.id=LIMINE_RSDP_REQUEST,.revision=0};
REQ struct limine_kernel_file_request kfreq={.id=LIMINE_KERNEL_FILE_REQUEST,.revision=0};
REQ struct limine_module_request modreq={.id=LIMINE_MODULE_REQUEST,.revision=0};

typedef struct __attribute__((packed)) { u16 lo,sel; u8 ist,flags; u16 mid; u32 hi,z; } idt_e;
static idt_e idt[48];
static struct __attribute__((packed)) { u16 lim; u64 base; } idtr,gdtr;
static u64 gdt[3]={0,0x00AF9A000000FFFF,0x00CF92000000FFFF};
extern u64 isr_table[]; void gdt_load(void*);
volatile u64 ticks; u64 mem_total_kb; static u64 hhdm; static int pit_ok; static u64 tsc0,tsc_tick;
u64 phys(const void*p){ return (u64)p-hhdm; }

typedef struct { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rbx,rdx,rcx,rax,no,err,rip,cs,rflags,rsp,ss; } regs;
static inline u64 rdtsc(void){ u32 a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); return (u64)d<<32|a; }
static void halt(void){ __asm__("cli"); for(;;)__asm__("hlt"); }
void panic(const char*s){ __asm__("cli"); outb(0xE9,'P'); char l[100]; ksnprintf(l,100,TR("Violyx angehalten: %s","Violyx stopped: %s"),s); ui_panic(l,0); halt(); }
void isr_handler(regs*r){
  if(r->no<32){ outb(0xE9,'P'); char d[100]; ksnprintf(d,100,TR("Ausnahme %d  Fehler %lx  RIP %lx","Exception %d  error %lx  RIP %lx"),(int)r->no,r->err,r->rip); ui_panic(TR("Violyx angehalten: CPU-Ausnahme","Violyx stopped: CPU exception"),d); halt(); }
  if(r->no==32){ ticks++; xhci_poll(); }
  else if(r->no==33) kbd_irq();
  if(r->no>=40) outb(0xA0,0x20);
  outb(0x20,0x20);
}
/* Wartet auf das naechste Ereignis. Ohne PIT (manche neuen Intel-Systeme) laeuft die Zeit ueber den TSC. */
void idle(void){ usb_hotplug(); if(pit_ok){ __asm__("sti;hlt"); net_poll(); return; } __asm__("pause"); ticks=(rdtsc()-tsc0)/tsc_tick; __asm__("cli"); xhci_poll(); __asm__("sti"); net_poll(); }
static void cpu_init(void){
  gdtr.lim=sizeof gdt-1; gdtr.base=(u64)gdt; gdt_load(&gdtr);
  for(int i=0;i<48;i++){ u64 a=isr_table[i]; idt[i]=(idt_e){(u16)a,0x08,0,0x8E,(u16)(a>>16),(u32)(a>>32),0}; }
  idtr.lim=sizeof idt-1; idtr.base=(u64)idt; __asm__ volatile("lidt %0"::"m"(idtr));
  outb(0x20,0x11);outb(0xA0,0x11);outb(0x21,32);outb(0xA1,40);outb(0x21,4);outb(0xA1,2);outb(0x21,1);outb(0xA1,1);
  outb(0x21,0xFC);outb(0xA1,0xFF);
  u32 div=1193182/100; outb(0x43,0x36);outb(0x40,div&255);outb(0x40,div>>8);
}
static void mem_init(void){
  struct limine_memmap_response*m=mmreq.response; u64 best=0,len=0;
  for(u64 i=0;i<m->entry_count;i++){ struct limine_memmap_entry*e=m->entries[i]; if(e->type!=LIMINE_MEMMAP_USABLE)continue; mem_total_kb+=e->length/1024; if(e->length>len){best=e->base;len=e->length;} }
  if(len<(1<<22))panic(TR("Zu wenig Speicher","Not enough memory"));
  heap_init(best+hhdm,best+hhdm+len);
}
/* Geraeteregister (MMIO) werden in der unteren Haelfte 1:1 und ungecacht eingeblendet.
   Das geschieht gezielt pro Geraet, weil sie je nach Firmware beliebig hoch liegen. */
static u64*pml4;
static u64*nexttbl(u64*t,int i){ if(!(t[i]&1)){ void*n=dma_alloc(4096); t[i]=phys(n)|3; } return (u64*)((t[i]&~0xFFFull)+hhdm); }
void mmio_map(u64 a,u64 n){ for(u64 p=a&~0x1FFFFFull;p<a+n;p+=0x200000){ u64*pdpt=nexttbl(pml4,(p>>39)&255),*pd=nexttbl(pdpt,(p>>30)&511); pd[(p>>21)&511]=p|3|(1<<3)|(1<<4)|(1<<7); }
  u64 c; __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3":"=r"(c)); }
static void paging_init(void){ u64 cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3)); pml4=(u64*)((cr3&~0xFFFull)+hhdm); for(int i=0;i<256;i++)pml4[i]=0; __asm__ volatile("mov %0,%%cr3"::"r"(cr3)); }
static u8 cmos(u8 r){ outb(0x70,r); return inb(0x71); }
static u8 bcd(u8 v){ return (v&15)+(v>>4)*10; }
static void pit_check(void){
  __asm__("sti"); u64 t=rdtsc(); for(u64 i=0;i<400000000&&!ticks;i++){ __asm__("pause"); if(rdtsc()-t>4000000000ull)break; }
  if(ticks){ pit_ok=1; return; }
  /* Fallback: TSC ueber die Sekundenumschaltung der CMOS-Uhr eichen */
  __asm__("cli"); u8 s=cmos(0); while(cmos(0)==s); u64 a=rdtsc(); s=cmos(0); while(cmos(0)==s); tsc_tick=(rdtsc()-a)/100; tsc0=rdtsc(); __asm__("sti");
}
/* Die Hardware-Uhr bleibt unberuehrt. Violyx speichert die Abweichung zur
   gewuenschten Zeit, so bleibt sie auch in QEMU ueber Neustarts erhalten. */
static int time_off; int cfg_wall,cfg_lang=1;
typedef struct { u32 magic; int off,wall,lang,kbd; } cfg_t;
#define CFG_NAME "/system/config"
static u32 to_secs(const rtc_t*t){ int y=t->year,m=t->mon; y-=m<=2; int era=y/400,yoe=y-era*400,doy=(153*(m+(m>2?-3:9))+2)/5+t->day-1,doe=yoe*365+yoe/4-yoe/100+doy;
  return (u32)(era*146097+doe-719468)*86400u+t->h*3600u+t->m*60u+t->s; }
u32 rtc_unix(const rtc_t*t){ return to_secs(t); }
void rtc_from_unix(u32 s,rtc_t*t);
static void from_secs(u32 s,rtc_t*t){ u32 d=s/86400,r=s%86400; t->h=r/3600;t->m=(r/60)%60;t->s=r%60;
  u32 z=d+719468,era=z/146097,doe=z-era*146097,yoe=(doe-doe/1460+doe/36524-doe/146096)/365,doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153,m=mp<10?mp+3:mp-9;
  t->day=doy-(153*mp+2)/5+1;t->mon=m;t->year=yoe+era*400+(m<=2); }
void rtc_from_unix(u32 s,rtc_t*t){ from_secs(s,t); }
static void rtc_raw(rtc_t*t){
  for(int i=0;i<100000&&(cmos(10)&0x80);i++);
  u8 fmt=cmos(11); u8 s=cmos(0),m=cmos(2),h=cmos(4),d=cmos(7),mo=cmos(8),y=cmos(9); u8 pm=h&0x80; h&=0x7F;
  if(!(fmt&4)){ s=bcd(s);m=bcd(m);h=bcd(h);d=bcd(d);mo=bcd(mo);y=bcd(y); }
  if(!(fmt&2)){ if(h==12)h=0; if(pm)h+=12; }
  t->h=h;t->m=m;t->s=s;t->day=d;t->mon=mo;t->year=2000+y; }
void rtc_read(rtc_t*t){ rtc_t r; rtc_raw(&r); from_secs(to_secs(&r)+(u32)time_off,t); }
void rtc_set(const rtc_t*t){ rtc_t r; rtc_raw(&r); time_off=(int)(to_secs(t)-to_secs(&r)); }
int cfg_save(void){ cfg_t c={0x32464356,time_off,cfg_wall,cfg_lang,cfg_kbd}; return fs_ok()?vfs_write(CFG_NAME,&c,sizeof c,fs_now()):-1; }
void cfg_load(void){ cfg_t c; if(fs_ok()&&vfs_read(CFG_NAME,&c,sizeof c)==sizeof c&&c.magic==0x32464356){ time_off=c.off; if(c.wall>=0&&c.wall<WALLS)cfg_wall=c.wall; cfg_lang=c.lang==1; cfg_kbd=c.kbd==1; } }
void reboot(void){ __asm__("cli"); outb(0xE9,'R'); for(int i=0;i<100000&&(inb(0x64)&2);i++); outb(0x64,0xFE); udelay(50000);
  outb(0xCF9,6); udelay(50000); struct __attribute__((packed)) { u16 l; u64 b; } z={0,0}; __asm__("lidt %0; int3"::"m"(z)); halt(); }
void poweroff(void){ outb(0xE9,'O'); u64 r=rsreq.response?(u64)rsreq.response->address:0; if(r>=hhdm)r-=hhdm; acpi_poweroff(r);
  ui_goodbye(TR("Violyx ist beendet.","Violyx has shut down."),TR("Du kannst den Rechner jetzt ausschalten.","You can now turn off the computer.")); halt(); }
int fs_live(void){ return kfreq.response&&kfreq.response->kernel_file&&kfreq.response->kernel_file->media_type==LIMINE_MEDIA_TYPE_OPTICAL; }
void kmain(void){
  if(!LIMINE_BASE_REVISION_SUPPORTED||!fbreq.response||!fbreq.response->framebuffer_count||!mmreq.response||!hhreq.response) halt();
  hhdm=hhreq.response->offset;
  struct limine_framebuffer*f=fbreq.response->framebuffers[0];
  fb_init(f->address,f->width,f->height,f->pitch,f->bpp,f->red_mask_shift,f->green_mask_shift,f->blue_mask_shift);
  ui_init();
  cpu_init(); mem_init(); paging_init();
  kbd_init(); pit_check();
  pci_t d[64]; int n=pci_scan(d,64);
  for(int i=0;i<n;i++){ if(d[i].cls==1&&d[i].sub==6&&d[i].pif==1) ahci_init(&d[i]); else if(d[i].cls==1&&d[i].sub==8&&d[i].pif==2) nvme_init(&d[i]); else if(d[i].cls==0xC&&d[i].sub==3&&d[i].pif==0x30) xhci_init(&d[i]); else if(d[i].cls==2&&d[i].sub==0&&d[i].ven==0x8086&&(d[i].did==0x100E||d[i].did==0x100F||d[i].did==0x1004||d[i].did==0x10D3)) e1000_init(&d[i]); }
  ide_probe(); if(modreq.response&&modreq.response->module_count) install_set_image(modreq.response->modules[0]->address,modreq.response->modules[0]->size); fs_init(); cfg_load(); drives_sync(); ui_init(); net_init(); ui_splash();
  app_help();
  ui_run();
}
