#include "os.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/raw.h"
#include "lwip/tcp.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "netif/ethernet.h"
/* Intel E1000 (Altkompatible Deskriptoren) und Anbindung an lwIP */
#define R(o) (*(volatile u32*)(base+(o)))
#define NR 32
#define NT 16
#define BSZ 2048
typedef struct __attribute__((packed)) { u64 addr; u16 len,csum; u8 st,err; u16 sp; } rxd;
typedef struct __attribute__((packed)) { u64 addr; u16 len; u8 cso,cmd,st,css; u16 sp; } txd;
static u64 base; static rxd*rd; static txd*td; static u8*rb,*tb; static int ri,ti; static struct netif nif; static u8 mac[6]; static int present,started; static u64 nextlink; static u32 seed;
unsigned int net_rand(void){ u32 a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); seed=seed*1103515245u+12345u+a; return seed^(d<<7); }
u32_t sys_now(void){ return (u32_t)(ticks*10); }
static int use_rge; int rge_attach(const pci_t*,u8*); int rge_link(void); int rge_send(const void*,int); int rge_recv(void**); void rge_recv_done(void);
static err_t tx(struct netif*n,struct pbuf*p){ if(p->tot_len>BSZ)return ERR_BUF; if(use_rge){ static u8 b[BSZ]; pbuf_copy_partial(p,b,p->tot_len,0); return rge_send(b,p->tot_len)?ERR_TIMEOUT:ERR_OK; } txd*d=&td[ti]; for(int i=0;i<200000&&!(d->st&1);i++)udelay(1); if(!(d->st&1))return ERR_TIMEOUT; pbuf_copy_partial(p,tb+ti*BSZ,p->tot_len,0); d->len=p->tot_len; d->st=0; d->cmd=1|2|8; ti=(ti+1)%NT; R(0x3818)=ti; return ERR_OK; }
static err_t nif_init(struct netif*n){ n->name[0]='e';n->name[1]='0'; n->output=etharp_output; n->linkoutput=tx; n->mtu=1500; n->hwaddr_len=6; memcpy(n->hwaddr,mac,6); n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_ETHERNET; return ERR_OK; }
#include "lwip/dhcp.h"
void net_diag(void(*cb)(const char*,void*),void*a){ char l[72]; rge_diag(cb,a); if(!started)return; struct dhcp*d=netif_dhcp_data(&nif); ksnprintf(l,72,"lwip state %d tries %d link %d up %d ip %d",d?d->state:-1,d?d->tries:-1,netif_is_link_up(&nif)?1:0,netif_is_up(&nif)?1:0,dhcp_supplied_address(&nif)?1:0); cb(l,a); }
void rge_probe(const pci_t*c){ if(present)return; if(!rge_attach(c,mac)){ use_rge=1; present=1; } }
void e1000_init(const pci_t*c){ if(present)return; pci_enable(c); base=pci_bar(c,0); if(!base)return; mmio_map(base,0x20000);
  R(0xD8)=~0u; R(0)|=1u<<26; for(int i=0;i<200000&&(R(0)&(1u<<26));i++)udelay(1); udelay(2000); R(0xD8)=~0u; R(0)|=(1<<6)|(1<<5);
  u32 lo=R(0x5400),hi=R(0x5404); if(!lo&&!(hi&0xFFFF)){ u16 w[3]; for(int i=0;i<3;i++){ R(0x14)=1|(i<<8); u32 v=0; for(int k=0;k<100000;k++){ v=R(0x14); if(v&0x10)break; udelay(1); } w[i]=v>>16; } lo=w[0]|w[1]<<16; hi=w[2]; }
  for(int i=0;i<4;i++)mac[i]=lo>>(i*8); mac[4]=hi;mac[5]=hi>>8; R(0x5400)=lo; R(0x5404)=(hi&0xFFFF)|(1u<<31); for(int i=0;i<128;i++)R(0x5200+i*4)=0;
  rd=dma_alloc(NR*sizeof(rxd)); td=dma_alloc(NT*sizeof(txd)); rb=dma_alloc(NR*BSZ); tb=dma_alloc(NT*BSZ);
  for(int i=0;i<NR;i++)rd[i].addr=phys(rb+i*BSZ); for(int i=0;i<NT;i++){ td[i].addr=phys(tb+i*BSZ); td[i].st=1; }
  u64 p=phys(rd); R(0x2800)=p;R(0x2804)=p>>32;R(0x2808)=NR*sizeof(rxd);R(0x2810)=0;R(0x2818)=NR-1; R(0x100)=2|(1<<15)|(1<<26);
  p=phys(td); R(0x3800)=p;R(0x3804)=p>>32;R(0x3808)=NT*sizeof(txd);R(0x3810)=0;R(0x3818)=0; R(0x400)=2|8|(0x10<<4)|(0x40<<12); R(0x410)=0x0060200A; present=1; }
void net_init(void){ if(!present)return; lwip_init(); ip4_addr_t z; ip4_addr_set_zero(&z); netif_add(&nif,&z,&z,&z,0,nif_init,ethernet_input); netif_set_default(&nif); netif_set_up(&nif); dhcp_start(&nif); started=1; }
int net_link(void);
void net_poll(void){ if(!started)return; if(use_rge){ for(int i=0;i<NR;i++){ void*b; int l=rge_recv(&b); if(l<=0)break; struct pbuf*p=pbuf_alloc(PBUF_RAW,l,PBUF_POOL); if(p){ pbuf_take(p,b,l); if(nif.input(p,&nif)!=ERR_OK)pbuf_free(p); } rge_recv_done(); } } else for(int i=0;i<NR;i++){ rxd*d=&rd[ri]; if(!(d->st&1))break; if((d->st&2)&&d->len&&d->len<=BSZ){ struct pbuf*p=pbuf_alloc(PBUF_RAW,d->len,PBUF_POOL); if(p){ pbuf_take(p,rb+ri*BSZ,d->len); if(nif.input(p,&nif)!=ERR_OK)pbuf_free(p); } } d->st=0; R(0x2818)=ri; ri=(ri+1)%NR; }
  if(ticks>=nextlink){ nextlink=ticks+50; int up=net_link(); if(up&&!netif_is_link_up(&nif)){ netif_set_link_up(&nif); dhcp_network_changed_link_up(&nif); } else if(!up&&netif_is_link_up(&nif))netif_set_link_down(&nif); }
  sys_check_timeouts(); }
int net_present(void){return present;} int net_ready(void){ return started&&dhcp_supplied_address(&nif); } int net_link(void){ return present&&(use_rge?rge_link():(R(8)&2)!=0); }
static void ipstr(const ip4_addr_t*a,char*o){ ksnprintf(o,16,"%d.%d.%d.%d",ip4_addr1(a),ip4_addr2(a),ip4_addr3(a),ip4_addr4(a)); }
const char*net_status(void){ static char b[48]; if(!present)return TR("keine Netzwerkkarte","no network card"); if(!net_link())return TR("kein Kabel","no cable"); if(!net_ready())return TR("warte auf DHCP","waiting for DHCP"); char s[16]; ipstr(netif_ip4_addr(&nif),s); ksnprintf(b,48,"%s",s); return b; }
void net_info(char*o,int n,int line){ o[0]=0; char a[16]; switch(line){ case 0: ksnprintf(o,n,"Status:  %s",net_status()); break; case 1: if(present)ksnprintf(o,n,"MAC:     %02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]); break;
    case 2: if(net_ready()){ ipstr(netif_ip4_addr(&nif),a); ksnprintf(o,n,TR("Adresse: %s","Address: %s"),a); } break; case 3: if(net_ready()){ ipstr(netif_ip4_netmask(&nif),a); ksnprintf(o,n,TR("Maske:   %s","Mask:    %s"),a); } break; case 4: if(net_ready()){ ipstr(netif_ip4_gw(&nif),a); ksnprintf(o,n,"Router:  %s",a); } break; case 5: if(net_ready()){ const ip_addr_t*d=dns_getserver(0); ipstr(d,a); ksnprintf(o,n,"DNS:     %s",a); } break; } }
/* Warten mit Abbruch per Esc */
int net_aborted,net_bg; static int esc(void){ key_t k; if(net_bg){ if(key_down(1)){ net_aborted=1; return 1; } return 0; } while(key_poll(&k)) if(k.key==K_ESC){ net_aborted=1; return 1; } return 0; }
static volatile int dns_st; static ip_addr_t dns_ip;
static void dns_cb(const char*n,const ip_addr_t*a,void*x){ if(a){dns_ip=*a;dns_st=1;} else dns_st=-1; }
int net_resolve(const char*name,u32*ip,char*txt){ if(!net_ready())return -3; dns_st=0; err_t e=dns_gethostbyname(name,&dns_ip,dns_cb,0); if(e==ERR_OK)dns_st=1; else if(e!=ERR_INPROGRESS)return -1; u64 end=ticks+600; while(!dns_st&&ticks<end){ idle(); if(esc())return -2; } if(dns_st!=1)return -1; *ip=ip4_addr_get_u32(&dns_ip); ipstr(&dns_ip,txt); return 0; }
static struct raw_pcb*pp; static volatile int got; static u16 wseq; static u8 rttl;
static u8_t ping_rx(void*a,struct raw_pcb*pc,struct pbuf*p,const ip_addr_t*f){ u8 b[28]; if(pbuf_copy_partial(p,b,28,0)<28)return 0; struct ip_hdr*h=(void*)b; int hl=IPH_HL(h)*4; struct icmp_echo_hdr e; if(pbuf_copy_partial(p,&e,sizeof e,hl)<sizeof e)return 0; if(e.type==ICMP_ER&&e.id==0x5649&&lwip_ntohs(e.seqno)==wseq){ got=1; rttl=IPH_TTL(h); pbuf_free(p); return 1; } return 0; }
int net_ping(u32 ip,u16 seq,int*ms,int*ttl){ if(!net_ready())return -3; if(!pp){ pp=raw_new(IP_PROTO_ICMP); if(!pp)return -1; raw_recv(pp,ping_rx,0); raw_bind(pp,IP_ADDR_ANY); } struct pbuf*p=pbuf_alloc(PBUF_IP,sizeof(struct icmp_echo_hdr)+32,PBUF_RAM); if(!p)return -1;
  struct icmp_echo_hdr*e=p->payload; e->type=ICMP_ECHO;e->code=0;e->chksum=0;e->id=0x5649;e->seqno=lwip_htons(seq); memset(e+1,'V',32); e->chksum=inet_chksum(e,p->len); wseq=seq; got=0; ip_addr_t d; ip4_addr_set_u32(&d,ip); u64 t0=ticks; err_t r=raw_sendto(pp,p,&d); pbuf_free(p); if(r!=ERR_OK)return -1;
  u64 end=t0+200; while(!got&&ticks<end){ idle(); if(esc())return -2; } if(!got)return 1; *ms=(int)(ticks-t0)*10; *ttl=rttl; return 0; }

/* TCP-Client, blockierend mit Esc-Abbruch; Empfang ueber einen Ringpuffer */
#define RXS (256*1024)
static struct tcp_pcb*tp; static u8*rx; static volatile int rh,rt,conn,fin,terr;
static int rcount(void){ return (rh-rt+RXS)%RXS; }
static err_t t_recv(void*a,struct tcp_pcb*p,struct pbuf*b,err_t e){ if(!b){ fin=1; return ERR_OK; } if(rcount()+b->tot_len>=RXS-1)return ERR_MEM; for(struct pbuf*q=b;q;q=q->next){ u8*d=q->payload; for(int i=0;i<q->len;i++){ rx[rh]=d[i]; rh=(rh+1)%RXS; } } pbuf_free(b); return ERR_OK; }
static err_t t_conn(void*a,struct tcp_pcb*p,err_t e){ conn=1; return ERR_OK; }
static void t_err(void*a,err_t e){ tp=0; terr=1; fin=1; }
static err_t t_sent(void*a,struct tcp_pcb*p,u16_t l){ return ERR_OK; }
int tcp_open(u32 ip,u16 port){ if(!net_ready())return -1; if(!rx)rx=kmalloc(RXS); if(!rx)return -1; rh=rt=0;conn=0;fin=0;terr=0; tp=tcp_new(); if(!tp)return -1; tcp_arg(tp,0); tcp_recv(tp,t_recv); tcp_err(tp,t_err); tcp_sent(tp,t_sent); ip_addr_t d; ip4_addr_set_u32(&d,ip); if(tcp_connect(tp,&d,port,t_conn)!=ERR_OK)return -1; u64 end=ticks+1500; while(!conn&&!terr&&ticks<end){ idle(); if(esc())return -2; } return conn?0:-1; }
int tcp_tx(const void*b,size_t n){ u64 end=ticks+3000; while(1){ if(!tp||terr)return -1; int room=tcp_sndbuf(tp); if(room>0){ int k=(size_t)room<n?room:(int)n; if(tcp_write(tp,b,k,TCP_WRITE_FLAG_COPY)==ERR_OK){ tcp_output(tp); return k; } } if(ticks>end)return -1; idle(); if(esc())return -1; } }
int tcp_tx_all(const void*b,int n){ int o=0; while(o<n){ int r=tcp_tx((const u8*)b+o,n-o); if(r<0)return -1; o+=r; } return o; }
int tcp_rx(void*b,size_t n){ u64 end=ticks+3000; while(!rcount()){ if(fin)return terr?-1:0; if(ticks>end)return -1; idle(); if(esc())return -1; } u8*d=b; size_t k=0; while(k<n&&rcount()){ d[k++]=rx[rt]; rt=(rt+1)%RXS; } if(tp)tcp_recved(tp,k); return k; }
void tcp_end(void){ if(tp){ tcp_arg(tp,0); tcp_recv(tp,0); tcp_err(tp,0); tcp_sent(tp,0); if(tcp_close(tp)!=ERR_OK)tcp_abort(tp); tp=0; } }
