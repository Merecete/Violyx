/* Host-Test rge: gcc -g -fsanitize=address,undefined -DHOST -Itools -Ikernel tools/rgetest.c -o rgetest
   Das Modell prueft nur Treiberlogik (Ringe, Grenzen, Zeitueberschreitungen), nicht das Verhalten des echten Chips. */
#include <stdarg.h>
#undef printf
#include "rge.c"
#undef printf
static u8 regs[0x10000]; static u32 hw=0x64a00000; static int dead; static u64 base=0x1000; static long txseen,txbad,wr_cmd;
static u64 pbase; void pci_enable(const pci_t*p){} u64 pci_bar(const pci_t*p,int n){ return n==2?base:0; } void mmio_map(u64 a,u64 b){} void udelay(int n){}
void*dma_alloc(u64 n){ void*p=aligned_alloc(4096,(n+4095)&~4095ull); memset(p,0,n); return p; } u64 phys(const void*p){ return (u64)p; }
int ksnprintf(char*o,int n,const char*f,...){ va_list a; va_start(a,f); int r=vsnprintf(o,n,f,a); va_end(a); return r; }
u32 mock_rd(u32 o,int w){ if(dead)return 0xFFFFFFFF; if(o==RGE_TXCFG)return hw|0x700; if(o==RGE_CMD)return regs[o]&~RGE_CMD_RESET; if(o==RGE_MCUCMD)return 0xFF; if(o==RGE_IM)return 0x0103; if(o==RGE_PHYSTAT)return RGE_PHYSTAT_LINK|RGE_PHYSTAT_1000MBPS; u32 v=0; memcpy(&v,regs+o,w); return v; }
static void txpump(void); void mock_wr(u32 o,u32 v,int w){ if(o==RGE_ISR){ u32 c; memcpy(&c,regs+o,4); c&=~v; memcpy(regs+o,&c,4); return; } memcpy(regs+o,&v,w); if(o==RGE_TXSTART)txpump(); if(o==RGE_MACOCP||o==RGE_PHYOCP){ regs[o+3]^=0; } }
static int hwtx; static void txpump(void){ for(;;){ struct rge_tx_desc*d=&tl[hwtx]; if(!(d->rge_cmdsts&RGE_TDCMDSTS_OWN))return; int l=d->rge_cmdsts&0xFFFF; if(l<60||l>BUFSZ||!(d->rge_cmdsts&RGE_TDCMDSTS_SOF)||!(d->rge_cmdsts&RGE_TDCMDSTS_EOF)||(((d->rge_cmdsts&RGE_TDCMDSTS_EOR)!=0)!=(hwtx==NTX-1))||d->rge_addr<(u64)tb||d->rge_addr+l>(u64)tb+NTX*BUFSZ)txbad++; volatile u8 s=*(u8*)d->rge_addr; (void)s; txseen++; d->rge_cmdsts&=~RGE_TDCMDSTS_OWN; hwtx=(hwtx+1)%NTX; } }
static int hwrx; static int inject(int l,u32 fl){ struct rge_rx_desc*r=&rl[hwrx]; u32 c=r->hi_qword1.rx_qword4.rge_cmdsts; if(!(c&RGE_RDCMDSTS_OWN))return 0; int cap=c&RGE_RDCMDSTS_FRAGLEN; if(cap!=BUFSZ||r->hi_qword0.rge_addr<(u64)rb||r->hi_qword0.rge_addr+BUFSZ>(u64)rb+NRX*BUFSZ)abort(); memset((void*)r->hi_qword0.rge_addr,0x5A,l>BUFSZ?BUFSZ:l); r->hi_qword1.rx_qword4.rge_cmdsts=(c&RGE_RDCMDSTS_EOR)|fl|l; hwrx=(hwrx+1)%NRX; return 1; }
static void cb(const char*l,void*a){ printf("  %s\n",l); }
int main(void){ pci_t p; u8 mac[6]; static u8 pk[3000]; regs[RGE_MAC0]=2;
 for(int k=0;k<3;k++){ hw=k==0?0x64a00000:k==1?0x64900000:0x64100000; stage=0; printf("Typ %x: attach %d\n",hw,rge_attach(&p,mac)); memset(tb,0,1); hwtx=hwrx=txi=rxi=0; memset(tl,0,4096); tl[NTX-1].rge_cmdsts=RGE_TDCMDSTS_EOR; for(int i=0;i<NRX;i++)rx_arm(i); }
 for(long i=0;i<300000;i++){ int l=1+rand()%2100; int r=rge_send(pk,l); if(l>BUFSZ){ if(!r)abort(); } else if(r)abort(); if(rand()%2){ int w=rand()%6==0?0:RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF; if(rand()%6==0)w|=RGE_RDCMDSTS_RXERRSUM; inject(rand()%20==0?rand()%0x3FFF:64+rand()%1400,w); } if(rand()%3==0){ void*b; int n; while((n=rge_recv(&b))>0){ if(n>BUFSZ||(u8*)b<rb||(u8*)b+n>rb+NRX*BUFSZ)abort(); volatile u8 s=((u8*)b)[n-1]; (void)s; rge_recv_done(); } } }
 printf("gesendet %ld, fehlerhaft %ld\n",txseen,txbad); rge_diag(cb,0); if(txbad)abort();
 /* Chip antwortet nicht mehr: nichts darf haengen */
 dead=1; printf("toter Chip: send %d link %d\n",rge_send(pk,100),rge_link()); hw=0xFFFFFFFF; stage=0; printf("toter Chip: attach %d\n",rge_attach(&p,mac)); return 0; }
