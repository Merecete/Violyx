/* Host-Test: gcc -g -fsanitize=address,undefined -Ikernel tools/vfstest.c -o vfstest && ./vfstest 1 1500 96 */
/* Zufallsoperationen, Vergleichsmodell, Strukturpruefung und simulierte Stromausfaelle */
#define HOST
#include "viofs.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SEC 512
static unsigned char *used;
static void mark(u32 b){ if(!b)return; if(b<sb.data_start||b>=sb.blocks){ puts("Block ausserhalb"); abort(); } if(used[b]){ printf("Block %u doppelt vergeben\n",b); abort(); } used[b]=1; if(bit(b)!=1){ printf("Block %u nicht in Bitmap\n",b); abort(); } }
static void marki(inode*i){ for(int k=0;k<NDIR;k++)mark(i->d[k]); if(i->ind){ mark(i->ind); u32 t[PB]; memcpy(t,bget(i->ind),BS); for(u32 k=0;k<PB;k++)mark(t[k]); } if(i->dind){ mark(i->dind); u32 t[PB],t2[PB]; memcpy(t,bget(i->dind),BS); for(u32 j=0;j<PB;j++){ if(!t[j])continue; mark(t[j]); memcpy(t2,bget(t[j]),BS); for(u32 k=0;k<PB;k++)mark(t2[k]); } } }
static int leaks;
static void walk(u32 n){ inode d; if(iget(n,&d))abort(); marki(&d); if(d.type!=T_DIR)return; u64 c=d.size/BS; for(u64 k=0;k<c;k++){ u32 b=bmap(&d,k,0); if(!b)continue; static u8 sv[32][BS]; static int lvl; if(lvl>=30)abort(); u8*m=sv[lvl++]; memcpy(m,bget(b),BS); for(int s=0;s<BS/DE;s++){ dirent*x=(dirent*)(m+s*DE); if(!x->ino)continue; inode i; if(iget(x->ino,&i)||i.type==T_FREE){ printf("Eintrag zeigt auf freie Inode %u\n",x->ino); abort(); } walk(x->ino); } lvl--; } }
static void fsck(void){ memset(used,0,sb.blocks); walk(1); int l=0; for(u32 b=sb.data_start;b<sb.blocks;b++) if(bit(b)==1&&!used[b])l++; if(l>leaks)leaks=l; }
static unsigned char *disk; static uint64_t ns; static long budget=-1; static unsigned char *fsd;
static int r_(uint64_t s,void*b){ if(s>=ns)abort(); memcpy(b,disk+s*SEC,SEC); return 0; }
static int w_(uint64_t s,const void*b){ if(s>=ns)abort(); if(budget==0)return -1; if(budget>0)budget--; memcpy(disk+s*SEC,b,SEC); return 0; }
static int f_(void){ return 0; }
static vfs_dev D;
#define MAXN 400
static struct { char p[200]; int dir; unsigned char *d; uint64_t l; } M[MAXN]; static int nm;
static int mfind(const char*p){ for(int i=0;i<nm;i++) if(!strcmp(M[i].p,p))return i; return -1; }
static void rnd_path(char*o,int dirs){ static const char*n[]={"a","bb","home","x y","lang_name_abc","ü"}; int d=1+rand()%3; o[0]=0; for(int i=0;i<d;i++){ strcat(o,"/"); strcat(o,n[rand()%6]); } }
static int parent_ok(const char*p){ char t[200]; strcpy(t,p); char*s=strrchr(t,'/'); if(s==t)return 1; *s=0; int i=mfind(t); return i>=0&&M[i].dir; }
static int cnt; static int cb(const vfs_ent*e,void*a){ cnt++; return 0; }
static void check(void){ static unsigned char *b; if(!b)b=malloc(1<<25); for(int i=0;i<nm;i++){ vfs_ent e; int r=vfs_stat(M[i].p,&e); if(r){ printf("fehlt %s %d\n",M[i].p,r); abort(); } if(e.dir!=M[i].dir)abort(); if(!M[i].dir){ int64_t n=vfs_read(M[i].p,b,1<<25); if(n!=(int64_t)M[i].l||memcmp(b,M[i].d,n)){ printf("Inhalt %s\n",M[i].p); abort(); } } } }
int main(int c,char**v){ srand(atoi(v[1])); int iters=atoi(v[2]); ns=(uint64_t)atoi(v[3])*2048; disk=calloc(ns,SEC); used=malloc(ns/8+1); D=(vfs_dev){r_,w_,f_,0,ns}; if(vfs_format(&D,1))abort(); uint64_t t,f0,f; vfs_usage(&t,&f0,0);
 for(int it=0;it<iters;it++){ char p[200],q[200]; rnd_path(p,0); int op=rand()%10; int i=mfind(p);
  if(op<5){ uint64_t l=rand()%40==0?(uint64_t)(rand()%12000000):rand()%4==0?rand()%300000:rand()%9000; unsigned char*d=malloc(l+1); for(uint64_t k=0;k<l;k+=64)d[k]=rand(); memset(d,it,l>8?8:l); int r=vfs_write(p,d,l,it); int pk=parent_ok(p); if(i>=0&&M[i].dir){ if(r!=VE_ISDIR){ printf("%d\n",r); abort(); } free(d); } else if(!pk){ if(!r)abort(); free(d); } else if(r==VE_NOSPC){ free(d); } else if(r){ printf("write %d\n",r); abort(); } else { if(i<0&&nm<MAXN){ i=nm++; strcpy(M[i].p,p); M[i].dir=0; M[i].d=0; } else if(i<0){ vfs_remove(p); free(d); continue; } free(M[i].d); M[i].d=d; M[i].l=l; } }
  else if(op<7){ int r=vfs_mkdir(p,it); if(i>=0){ if(r!=VE_EXIST)abort(); } else if(!parent_ok(p)){ if(!r)abort(); } else if(r&&r!=VE_NOSPC)abort(); else if(!r&&nm<MAXN){ i=nm++; strcpy(M[i].p,p); M[i].dir=1; M[i].d=0; M[i].l=0; } else if(!r)vfs_remove(p); }
  else if(op<9&&i>=0){ int kids=0; size_t l=strlen(p); for(int k=0;k<nm;k++) if(!strncmp(M[k].p,p,l)&&M[k].p[l]=='/')kids++; int r=vfs_remove(p); if(kids){ if(r!=VE_NOTEMPTY)abort(); } else { if(r)abort(); free(M[i].d); M[i]=M[--nm]; } }
  else if(i>=0&&!M[i].dir){ rnd_path(q,0); if(mfind(q)<0&&parent_ok(q)&&strcmp(p,q)&&strncmp(q,p,strlen(p))){ int r=vfs_rename(p,q); if(r&&r!=VE_NOSPC){ printf("ren %d\n",r); abort(); } if(!r)strcpy(M[i].p,q); } }
  if(it%40==0){ check(); fsck(); vfs_unmount(); if(vfs_mount(&D))abort(); check(); }
  /* Stromausfall: Schreiben bricht nach zufaelliger Sektorzahl ab */
  if(it%15==7){ int fi=-1; for(int k=0;k<nm;k++) if(!M[k].dir)fi=k; budget=rand()%400; rnd_path(q,0); int j=mfind(q); static unsigned char d2[120000]; memset(d2,0x5A,sizeof d2); int ok=0; if(rand()%2&&fi>=0){ ok=!vfs_write(M[fi].p,d2,sizeof d2,it); j=fi; } else { vfs_mkdir(q,it); } budget=-1; vfs_unmount(); if(vfs_mount(&D))abort(); if(j>=0&&j==fi){ int64_t n=vfs_read(M[j].p,d2,sizeof d2); static unsigned char *b; (void)b; if(ok&&n!=sizeof d2)abort(); if(n==sizeof d2&&d2[0]==0x5A&&d2[119999]==0x5A){ free(M[j].d); M[j].d=malloc(sizeof d2); memcpy(M[j].d,d2,sizeof d2); M[j].l=sizeof d2; } } vfs_ent e; int had=mfind(q); if(had<0&&!vfs_stat(q,&e)){ if(e.dir&&nm<MAXN){ int i2=nm++; strcpy(M[i2].p,q); M[i2].dir=1; M[i2].d=0; M[i2].l=0; } else vfs_remove(q); } check(); fsck(); }
 }
 check(); cnt=0; vfs_list("/",cb,0); vfs_usage(&t,&f,0); fsck(); printf("verlorene Bloecke max %d, ",leaks); printf("ok: %d Eintraege, %d im Wurzelordner, frei %lu von %lu KB\n",nm,cnt,(unsigned long)(f>>10),(unsigned long)(t>>10)); return 0; }
