/* Ersatz fuer os.h beim Host-Test des rge-Treibers: Modell des Chips, siehe tools/rgetest.c */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef struct { int x; } pci_t;
u32 mock_rd(u32 o,int w); void mock_wr(u32 o,u32 v,int w); void pci_enable(const pci_t*); u64 pci_bar(const pci_t*,int); void mmio_map(u64,u64);
void*dma_alloc(u64); u64 phys(const void*); void udelay(int); int ksnprintf(char*,int,const char*,...);
#define RGE_READ_1(sc,o) ((u8)mock_rd(o,1))
#define RGE_READ_2(sc,o) ((u16)mock_rd(o,2))
#define RGE_READ_4(sc,o) mock_rd(o,4)
#define RGE_WRITE_1(sc,o,v) mock_wr(o,(u8)(v),1)
#define RGE_WRITE_2(sc,o,v) mock_wr(o,(u16)(v),2)
#define RGE_WRITE_4(sc,o,v) mock_wr(o,(u32)(v),4)
