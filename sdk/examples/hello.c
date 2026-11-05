/* Kleinstes sinnvolles Programm: Text, Tasten, eine gespeicherte Datei */
#include "violyx.h"
int main(void){ long starts=0; vx_read("starts",&starts,sizeof starts); starts++; vx_write("starts",&starts,sizeof starts);
  uint8_t t=VX_ATTR(VC_BLACK,VC_GLOW),d=VX_ATTR(VC_BLACK,VC_MID); int last=0;
  for(;;){ vx_clear(d); vx_text(2,1,vx_english()?"Hello from a real Violyx program!":"Hallo aus einem echten Violyx-Programm!",t); vx_text(2,3,vx_english()?"Started this many times:":"So oft gestartet:",d); vx_number(30,3,starts,t); vx_text(2,5,vx_english()?"Last key:":"Letzte Taste:",d); vx_number(30,5,last,t); vx_text(2,7,vx_english()?"Q quits, Esc closes the window":"Q beendet, Esc schliesst das Fenster",d); vx_show();
    vx_wait(-1); int k=vx_key(); if(k)last=k; if(k=='q'||k=='Q')return 0; } }
