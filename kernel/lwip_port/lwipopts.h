#pragma once
/* lwIP without an operating system layer: everything runs in the Violyx main loop */
#define NO_SYS                   1
#define SYS_LIGHTWEIGHT_PROT     0
#define LWIP_SOCKET              0
#define LWIP_NETCONN             0
#define LWIP_RAW                 1
#define LWIP_ICMP                1
#define LWIP_DHCP                1
#define LWIP_DNS                 1
#define DNS_MAX_SERVERS          2
#define LWIP_UDP                 1
#define LWIP_TCP                 1
#define LWIP_IPV4                1
#define LWIP_IPV6                0
#define LWIP_AUTOIP              0
#define LWIP_ARP                 1
#define LWIP_ETHERNET            1
#define LWIP_STATS               0
#define LWIP_NETIF_LINK_CALLBACK 0
#define MEM_ALIGNMENT            8
#define MEM_SIZE                 (1024 * 1024)
#define MEMP_NUM_PBUF            64
#define MEMP_NUM_TCP_PCB         16
#define MEMP_NUM_TCP_SEG         128
#define MEMP_NUM_UDP_PCB         8
#define PBUF_POOL_SIZE           128
#define TCP_MSS                  1460
#define TCP_WND                  (16 * TCP_MSS)
#define TCP_SND_BUF              (16 * TCP_MSS)
#define TCP_SND_QUEUELEN         64
