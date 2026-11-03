/* Host-Test FAT32: gcc -g -fsanitize=address,undefined -DHOST -Ibuild -Ikernel tools/fattest.c kernel/fat.c kernel/viofs.c -o fattest
   Aufruf modell: fattest m ABBILD SEED ITER; beschaedigt: fattest f ABBILD SEED ITER */
#include "fat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char*disk; static long ns; 
static int r_(uint64_t s,void*b){ if((long)s>=ns)return -1; memcpy(b,disk+s*512,512); return 0; }
static int w_(uint64_t s,const void*b){ if((long)s>=ns)return -1; memcpy(disk+s*512,b,512); return 0; }
static int f_(void){ return 0; }
static fatvol V; static vfs_dev D;
#define MAXN 300
static struct { char p[600]; int dir; unsigned char*d; uint64_t l; } M[MAXN]; static int nm;
static int find(const char*p){ for(int i=0;i<nm;i++) if(!strcasecmp(M[i].p,p))return i; return -1; }
static const char*NM[]={"a.txt","Lange Datei mit Namen.text","dir","UPPER.TXT","x y z","\x84pfel.dat","kurz","sehr_sehr_sehr_langer_dateiname_der_ueber_mehrere_eintraege_geht_und_noch_weiter.bin"};
static void rp(char*o){ int d=1+rand()%3; o[0]=0; for(int i=0;i<d;i++){ strcat(o,"/"); strcat(o,NM[rand()%8]); } }
static int pok(const char*p){ char t[600]; strcpy(t,p); char*s=strrchr(t,'/'); if(s==t)return 1; *s=0; int i=find(t); return i>=0&&M[i].dir; }
static int cb(const vfs_ent*e,void*a){ (*(int*)a)++; return 0; }
static void check(void){ static unsigned char*b; if(!b)b=malloc(1<<23); for(int i=0;i<nm;i++){ vfs_ent e; int r=fat_stat(&V,M[i].p,&e); if(r){ printf("fehlt %s %d\n",M[i].p,r); abort(); } if(e.dir!=M[i].dir)abort(); if(!e.dir){ int64_t n=fat_read(&V,M[i].p,b,1<<23); if(n!=(int64_t)M[i].l||memcmp(b,M[i].d,n)){ printf("Inhalt %s %ld/%lu\n",M[i].p,(long)n,(unsigned long)M[i].l); abort(); } } } int c=0; fat_list(&V,"/",cb,&c); }
int main(int ac,char**av){ FILE*f=fopen(av[2],"rb"); fseek(f,0,2); ns=ftell(f)/512; rewind(f); disk=malloc(ns*512); fread(disk,1,ns*512,f); fclose(f); srand(atoi(av[3])); int iters=atoi(av[4]); D=(vfs_dev){r_,w_,f_,0,(uint64_t)ns};
 if(av[1][0]=='m'){ if(fat_mount(&V,&D))abort(); for(int it=0;it<iters;it++){ char p[600],q[600]; rp(p); int i=find(p),op=rand()%10; if(op<5){ uint64_t l=rand()%20==0?rand()%3000000:rand()%3==0?0:rand()%20000; unsigned char*d=malloc(l+1); for(uint64_t k=0;k<l;k+=97)d[k]=rand(); int r=fat_write(&V,p,d,l,1700000000+it); if(i>=0&&M[i].dir){ if(r!=VE_ISDIR)abort(); free(d); } else if(!pok(p)){ if(!r)abort(); free(d); } else if(r==VE_NOSPC)free(d); else if(r){ printf("w %d %s\n",r,p); abort(); } else { if(i<0&&nm<MAXN){ i=nm++; strcpy(M[i].p,p); M[i].dir=0;M[i].d=0; } else if(i<0){ fat_remove(&V,p); free(d); continue; } free(M[i].d); M[i].d=d; M[i].l=l; } } else if(op<7){ int r=fat_mkdir(&V,p,1); if(i>=0){ if(r!=VE_EXIST)abort(); } else if(!pok(p)){ if(!r)abort(); } else if(!r&&nm<MAXN){ i=nm++; strcpy(M[i].p,p); M[i].dir=1;M[i].d=0;M[i].l=0; } else if(!r)fat_remove(&V,p); else if(r!=VE_NOSPC){ printf("md %d\n",r); abort(); } } else if(op<9&&i>=0){ int kids=0; size_t l=strlen(p); for(int k=0;k<nm;k++) if(!strncasecmp(M[k].p,p,l)&&M[k].p[l]=='/')kids++; int r=fat_remove(&V,p); if(kids){ if(r!=VE_NOTEMPTY)abort(); } else { if(r){ printf("rm %d\n",r); abort(); } free(M[i].d); M[i]=M[--nm]; } } else if(i>=0){ rp(q); if(find(q)<0&&pok(q)&&strcasecmp(p,q)&&strncasecmp(q,p,strlen(p))){ size_t l=strlen(p); int ch=0; for(int k=0;k<nm;k++) if(!strncasecmp(M[k].p,p,l)&&M[k].p[l]=='/')ch++; if(!ch&&fat_rename(&V,p,q)==0)strcpy(M[i].p,q); } } if(it%50==0){ check(); if(fat_mount(&V,&D))abort(); check(); } } check(); f=fopen(av[2],"wb"); fwrite(disk,1,ns*512,f); fclose(f); printf("ok: %d Eintraege im Modell\n",nm); return 0; }
 if(av[1][0]=='p'){ /* Datei ablegen: fattest p ABBILD 0 0 PFAD TEXT */ if(fat_mount(&V,&D))abort(); char t[600]; strcpy(t,av[5]); for(char*q=t+1;*q;q++) if(*q=='/'){ *q=0; fat_mkdir(&V,t,1700000000); *q='/'; } if(fat_write(&V,av[5],av[6],strlen(av[6]),1700000000))abort(); f=fopen(av[2],"wb"); fwrite(disk,1,ns*512,f); fclose(f); return 0; }
 /* Beschaedigte Abbilder: darf nie abstuerzen */
 unsigned char*orig=malloc(ns*512); memcpy(orig,disk,ns*512); static unsigned char b[1<<20]; long len=ns*512; for(int it=0;it<iters;it++){ long meta=4096*512; if(meta>len)meta=len; memcpy(disk,orig,meta); int nb=1+rand()%200; for(int k=0;k<nb;k++){ long o=rand()%(rand()%2?1024*64:meta); disk[o]=rand()%3?rand():(rand()%2?0xFF:0); } if(fat_mount(&V,&D))continue; char p[600]; rp(p); vfs_ent e; fat_stat(&V,p,&e); fat_read(&V,p,b,sizeof b); int c=0; fat_list(&V,"/",cb,&c); fat_list(&V,p,cb,&c); if(it%3==0){ fat_write(&V,p,b,rand()%5000,1); fat_mkdir(&V,p,1); fat_remove(&V,p); } } puts("Beschaedigte Abbilder: ueberstanden"); return 0; }
