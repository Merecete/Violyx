#include "os.h"
void *memset(void*d,int c,size_t n){ u8*p=d; while(n--)*p++=c; return d; }
void *memcpy(void*d,const void*s,size_t n){ u8*a=d; const u8*b=s; while(n--)*a++=*b++; return d; }
void *memmove(void*d,const void*s,size_t n){ u8*a=d; const u8*b=s;
  if(a<b) while(n--)*a++=*b++; else { a+=n;b+=n; while(n--)*--a=*--b; } return d; }
int memcmp(const void*x,const void*y,size_t n){ const u8*a=x,*b=y;
  for(;n--;a++,b++) if(*a!=*b) return *a-*b; return 0; }
size_t strlen(const char*s){ size_t n=0; while(s[n])n++; return n; }
int strcmp(const char*a,const char*b){ while(*a&&*a==*b){a++;b++;} return (u8)*a-(u8)*b; }
void strlcpy_(char*d,const char*s,size_t n){ size_t i=0; if(!n)return; for(;i+1<n&&s[i];i++)d[i]=s[i]; d[i]=0; }

int ksnprintf(char*o,size_t n,const char*f,...){
  va_list a; va_start(a,f); size_t p=0;
#define OUT(c) do{ if(p+1<n)o[p]=(c); p++; }while(0)
  for(;*f;f++){
    if(*f!='%'){ OUT(*f); continue; }
    f++; int zero=0,wd=0;
    if(*f=='0'){zero=1;f++;}
    while(*f>='0'&&*f<='9') wd=wd*10+*f++-'0';
    if(*f=='s'){ const char*s=va_arg(a,const char*); int l=strlen(s); for(int i=0;s[i];i++)OUT(s[i]); for(;l<wd;l++)OUT(' '); }
    else if(*f=='c') OUT((char)va_arg(a,int));
    else if(*f=='%') OUT('%');
    else { char t[24]; int i=0; u64 v; int neg=0; u32 base=(*f=='x'||(f[0]=='l'&&f[1]=='x'))?16:10;
      if(*f=='l'){ f++; v=va_arg(a,u64); } else if(*f=='d'){ int x=va_arg(a,int); neg=x<0; v=neg?(u32)-x:(u32)x; } else v=va_arg(a,u32);
      do{ t[i++]="0123456789abcdef"[v%base]; v/=base; }while(v);
      if(neg)t[i++]='-';
      for(int k=i;k<wd;k++)OUT(zero?'0':' ');
      while(i)OUT(t[--i]);
    }
  }
  if(n)o[p<n?p:n-1]=0;
  va_end(a); return p;
}

/* Heap: erste passende Luecke, Nachbarn werden beim Freigeben verschmolzen */
typedef struct blk { u64 size,free; struct blk*next; u64 pad; } blk;
static blk*head;
void heap_init(u64 s,u64 e){ head=(blk*)s; head->size=e-s-sizeof(blk); head->free=1; head->next=0; }
void *kmalloc(size_t n){ n=(n+15)&~15u;
  for(blk*b=head;b;b=b->next) if(b->free&&b->size>=n){
    if(b->size>=n+sizeof(blk)+64){ blk*r=(blk*)((u8*)(b+1)+n); r->size=b->size-n-sizeof(blk); r->free=1; r->next=b->next; b->next=r; b->size=n; }
    b->free=0; return b+1; }
  return 0; }
void kfree(void*p){ if(!p)return; ((blk*)p-1)->free=1;
  for(blk*b=head;b&&b->next;){ if(b->free&&b->next->free){ b->size+=b->next->size+sizeof(blk); b->next=b->next->next; } else b=b->next; } }
u64 heap_free(void){ u64 t=0; for(blk*b=head;b;b=b->next) if(b->free)t+=b->size; return t; }
void *dma_alloc(size_t n){ u8*p=kmalloc(n+8192); if(!p)panic("Kein DMA-Speicher"); p=(u8*)(((u64)p+4095)&~4095ull); memset(p,0,n); return p; }
int strncmp(const char*a,const char*b,size_t n){ for(;n;n--,a++,b++){ if(*a!=*b)return (u8)*a-(u8)*b; if(!*a)return 0; } return 0; }
int atoi(const char*s){ int v=0; while(*s>='0'&&*s<='9')v=v*10+*s++-'0'; return v; }
char *strchr(const char*s,int c){ for(;;s++){ if(*s==(char)c)return (char*)s; if(!*s)return 0; } }
char *strrchr(const char*s,int c){ const char*r=0; for(;;s++){ if(*s==(char)c)r=s; if(!*s)return (char*)r; } }
char *strstr(const char*h,const char*n){ size_t l=strlen(n); for(;*h;h++) if(!strncmp(h,n,l))return (char*)h; return l?0:(char*)h; }
