#include "violyx.h"
__asm__(".text\n.globl _start\n_start:\n  and $-16,%rsp\n  call main\n  xor %edi,%edi\n  xor %eax,%eax\n  int $48\n1: jmp 1b\n"
        "vx_sys:\n .globl vx_sys\n  mov %rdi,%rax\n  mov %rsi,%rdi\n  mov %rdx,%rsi\n  mov %rcx,%rdx\n  int $48\n  ret\n");
void *memcpy(void*d,const void*s,size_t n){ char*a=d; const char*b=s; while(n--)*a++=*b++; return d; }
void *memset(void*d,int c,size_t n){ char*a=d; while(n--)*a++=(char)c; return d; }
void *memmove(void*d,const void*s,size_t n){ char*a=d; const char*b=s; if(a<b)while(n--)*a++=*b++; else while(n--)a[n]=b[n]; return d; }
size_t strlen(const char*s){ size_t n=0; while(s[n])n++; return n; }
static uint16_t cells[80*25]; static int W,H;
static void dims(void){ if(!W){ long v=vx_sys(4,0,0,0); W=(int)(v>>16); H=(int)(v&0xFFFF); } }
int vx_cols(void){ dims(); return W; } int vx_rows(void){ dims(); return H; }
void vx_clear(uint8_t a){ dims(); for(int i=0;i<W*H;i++)cells[i]=(uint16_t)(' '|a<<8); }
void vx_put(int x,int y,char c,uint8_t a){ dims(); if(x>=0&&x<W&&y>=0&&y<H)cells[y*W+x]=(uint16_t)((uint8_t)c|a<<8); }
void vx_text(int x,int y,const char*s,uint8_t a){ for(;*s;s++)vx_put(x++,y,*s,a); }
void vx_number(int x,int y,long v,uint8_t a){ char b[24]; int n=0; if(v<0){ vx_put(x++,y,'-',a); v=-v; } do{ b[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)vx_put(x++,y,b[--n],a); }
void vx_show(void){ vx_sys(1,(long)cells,0,0); }
void vx_title(const char*s){ vx_sys(5,(long)s,0,0); }
void *vx_alloc(size_t n){ long p=vx_sys(8,(long)n,0,0); return p==-1?0:(void*)p; }
int vx_random(int n){ static uint32_t r; if(!r)r=(uint32_t)vx_ms()*2654435761u+vx_time(); r=r*1103515245u+12345u; return (int)((r>>16)%(uint32_t)n); }
