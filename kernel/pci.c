#include "os.h"
/* PCI-Erkennung ueber die klassischen Konfigurationsports */
static u32 addr(u8 b,u8 d,u8 f,u8 o){ return 0x80000000u|(b<<16)|(d<<11)|(f<<8)|(o&0xFC); }
static u32 rd(u8 b,u8 d,u8 f,u8 o){ outl(0xCF8,addr(b,d,f,o)); return inl(0xCFC); }
u32 pci_rd(const pci_t*p,u8 o){ return rd(p->bus,p->dev,p->fn,o); }
void pci_wr(const pci_t*p,u8 o,u32 v){ outl(0xCF8,addr(p->bus,p->dev,p->fn,o)); outl(0xCFC,v); }
void pci_enable(const pci_t*p){ pci_wr(p,4,pci_rd(p,4)|6); }
u64 pci_bar(const pci_t*p,int n){ u32 l=pci_rd(p,0x10+n*4); if(l&1)return 0; u64 a=l&~15u; if((l&6)==4) a|=(u64)pci_rd(p,0x14+n*4)<<32; return a; }
int pci_scan(pci_t*o,int max){ int n=0; for(int b=0;b<256;b++)for(int d=0;d<32;d++){ int fns=(rd(b,d,0,0xC)>>16&0x80)?8:1; for(int f=0;f<fns;f++){ u32 id=rd(b,d,f,0); if((id&0xFFFF)==0xFFFF)continue; u32 c=rd(b,d,f,8); if(n<max) o[n++]=(pci_t){b,d,f,c>>24,c>>16,c>>8,id,id>>16}; } } return n; }
