#include "os.h"
/* PS/2-Tastatur, Scancode-Satz 1, deutsches QWERTZ-Layout, Zeichen in CP437 */
static const u8 base[0x59]={0,0,'1','2','3','4','5','6','7','8','9','0',0xE1,0x27,0,0,
 'q','w','e','r','t','z','u','i','o','p',0x81,'+',0,0,'a','s','d','f','g','h','j','k','l',0x94,0x84,'^',0,'#',
 'y','x','c','v','b','n','m',',','.','-',0,'*',0,' ',0,[0x4A]='-',[0x4E]='+',[0x56]='<'};
static const u8 shft[0x59]={0,0,'!','"',0x15,'$','%','&','/','(',')','=','?','`',0,0,
 'Q','W','E','R','T','Z','U','I','O','P',0x9A,'*',0,0,'A','S','D','F','G','H','J','K','L',0x99,0x8E,0xF8,0,0x27,
 'Y','X','C','V','B','N','M',';',':','_',0,'*',0,' ',0,[0x4A]='-',[0x4E]='+',[0x56]='>'};
static const u8 altg[0x59]={[3]=0xFD,[8]='{',[9]='[',[10]=']',[11]='}',[12]='\\',[0x1B]='~',[0x10]='@',[0x32]=0xE6,[0x56]='|'};
static const u8 us_base[0x59]={0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,0,
 'q','w','e','r','t','y','u','i','o','p','[',']',0,0,'a','s','d','f','g','h','j','k','l',';',0x27,'`',0,'\\',
 'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0,[0x4A]='-',[0x4E]='+',[0x56]='\\'};
static const u8 us_shft[0x59]={0,0,'!','@','#','$','%','^','&','*','(',')','_','+',0,0,
 'Q','W','E','R','T','Y','U','I','O','P','{','}',0,0,'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
 'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0,[0x4A]='-',[0x4E]='+',[0x56]='|'};
int cfg_kbd;
static key_t q[64]; static volatile u8 qh,qt;
static u8 shift,ctrl,alt,altgr,caps,ext; static volatile u8 held[128];
int key_down(u8 s){ return held[s&127]; }
static void push(u16 k){ u8 n=(qh+1)&63; if(n==qt)return; q[qh].key=k;
  q[qh].mods=(shift?M_SHIFT:0)|(ctrl?M_CTRL:0)|(alt?M_ALT:0); qh=n; }
int key_poll(key_t*k){ if(qt==qh)return 0; *k=q[qt]; qt=(qt+1)&63; return 1; }
void kbd_init(void){ for(int i=0;i<64&&(inb(0x64)&1);i++) inb(0x60); }
static u16 nav(u8 s){ switch(s){ case 0x48:return K_UP;case 0x50:return K_DOWN;case 0x4B:return K_LEFT;case 0x4D:return K_RIGHT;
  case 0x47:return K_HOME;case 0x4F:return K_END;case 0x49:return K_PGUP;case 0x51:return K_PGDN;case 0x52:return K_INS;case 0x53:return K_DEL;} return 0; }
void kbd_irq(void){ kbd_in(inb(0x60)); }
void kbd_in(u8 s){
  if(s==0xE0){ ext=1; return; }
  if(s==0xE1){ ext=0; return; }
  u8 up=s&0x80, c=s&0x7F, e=ext; ext=0;
  if(e&&(c==0x2A||c==0x36)) return;           /* unechtes Shift */
  held[c]=!up;
  if(c==0x2A||c==0x36){ shift=!up; return; }
  if(c==0x1D){ ctrl=!up; return; }
  if(c==0x38){ if(e)altgr=!up; else alt=!up; return; }
  if(up) return;
  if(ctrl&&alt&&(c==0x53)){ outb(0xE9,'K'); push(K_REBOOT); return; }
  if(c==0x3A){ caps=!caps; return; }
  if(c==0x45||c==0x46) return;
  if(e){ u16 k=nav(c); if(k){push(k);return;} if(c==0x1C)push(K_ENTER); else if(c==0x35)push('/'); return; }
  if(c==0x01){push(K_ESC);return;} if(c==0x0E){push(K_BS);return;} if(c==0x0F){push(K_TAB);return;} if(c==0x1C){push(K_ENTER);return;}
  if(c>=0x3B&&c<=0x44){push(K_F1+c-0x3B);return;} if(c==0x57){push(K_F11);return;} if(c==0x58){push(K_F12);return;}
  { u16 k=nav(c); if(k){push(k);return;} }
  if(c>=0x59) return;
  u8 ch;
  if(!cfg_kbd&&(altgr||(ctrl&&alt))){ ch=altg[c]; if(ch){ u8 a=alt,t=ctrl; alt=0;ctrl=0; push(ch); alt=a;ctrl=t; } return; }
  ch=cfg_kbd?(shift?us_shft[c]:us_base[c]):(shift?shft[c]:base[c]);
  if(caps&&c<0x36){ if(ch>='a'&&ch<='z')ch-=32; else if(ch>='A'&&ch<='Z')ch+=32; }
  if(ch) push(ch);
}
