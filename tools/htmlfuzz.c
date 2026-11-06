/* Host-Test: gcc -g -fsanitize=address,undefined -Ibuild -Ikernel tools/htmlfuzz.c -o htmlfuzz */
#define HOST
#include "html.c"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
static hpage g;
static void al(int small){ free(g.l);free(g.ch);free(g.st);free(g.lk);free(g.fg);free(g.im);free(g.pool);free(g.links);free(g.anch);free(g.fld);free(g.frm); g.maxl=small?40:200000;g.l=malloc(sizeof(hline)*g.maxl);g.cap=small?2000:3000000;g.ch=malloc(g.cap);g.st=malloc(g.cap);g.lk=malloc(g.cap*2);g.fg=malloc(g.cap);g.maxim=small?1:32;g.im=calloc(g.maxim,sizeof(himg)); if(!small){ g.im[1].state=IM_OK; g.im[1].cols=30; g.im[1].rows=12; }g.pc=small?300:1<<20;g.pool=malloc(g.pc);g.maxlk=small?5:20000;g.links=malloc(sizeof(hlink)*g.maxlk);g.maxan=small?3:4096;g.anch=malloc(sizeof(hanchor)*g.maxan);g.maxf=small?2:256;g.fld=malloc(sizeof(hfield)*g.maxf);g.maxfrm=small?1:64;g.frm=malloc(sizeof(hform)*g.maxfrm);g.nums=rand()%2; }
static const char*dict[]={"<form action=\"","<form method=post>","</form>","<input name=a>","<input type=submit value='","<input type=hidden name=","<button>","</button>","<textarea name=x>","<input type=password>","<input name=b value=\"","<a href=\"","</a>","<ul>","</ul>","<li>","<blockquote>","</blockquote>","<pre>","</pre>","<table><tr><td>","<td>","<h1>","</h1>","&#x","&#","&amp","<!--","-->","<script>","<title>","<img alt='","\xF0\x9F","\xE2","\xC3","<br>","<hr>","<ol>","</ol>",">","<","\"","'","=","&","<svg","</","<!","<?"};
int main(int ac,char**av){ char*files[64]; size_t sz[64]; int nf=0; DIR*d=opendir(av[1]); struct dirent*e; char p[512]; while((e=readdir(d))&&nf<64){ if(e->d_name[0]=='.')continue; snprintf(p,512,"%s/%s",av[1],e->d_name); FILE*f=fopen(p,"rb"); fseek(f,0,2); long n=ftell(f); rewind(f); if(n<=0)continue; if(n>400000)n=400000; files[nf]=malloc(n); fread(files[nf],1,n,f); sz[nf++]=n; fclose(f); }
 long iters=atol(av[2]); srand(atoi(av[3])); char o[300]; for(int i=0;i<nf;i++){ al(0); html_layout(files[i],sz[i],78,0,0,&g); printf("Seite %d: %zu B, %d Zeilen, %d Links, full=%d, Titel %s\n",i,sz[i],g.nl,g.nlk,g.full,g.title); }
 for(long it=0;it<iters;it++){ int fi=rand()%nf; size_t base=sz[fi]; size_t st=rand()%base,l=1+rand()%(base-st<30000?base-st:30000); char*b=malloc(l+400); size_t k=l; memcpy(b,files[fi]+st,l); int mu=rand()%80; for(int m=0;m<mu;m++){ size_t q=rand()%k; int t=rand()%4; if(t==0)b[q]=rand(); else if(t==1)b[q]^=1<<(rand()%8); else if(t==2){ const char*s=dict[rand()%(sizeof dict/sizeof*dict)]; size_t sl_=strlen(s); if(k+sl_<l+400){ memmove(b+q+sl_,b+q,k-q); memcpy(b+q,s,sl_); k+=sl_; } } else if(k>2){ size_t c=rand()%(k-q); memmove(b+q,b+q+c,k-q-c); k-=c; if(!k)k=1; } }
   char*x=malloc(k); memcpy(x,b,k); free(b); if(it%50==0)al(rand()%2); if(g.l&&g.maxl==40&&it%50!=0)al(1); int w=20+rand()%140; html_layout(x,k,w,rand()%4==0,rand()%10==0,&g);
   for(int i=0;i<g.nl;i++){ if(g.l[i].off+g.l[i].len>g.used){ puts("Zeile ausserhalb"); abort(); } if(g.l[i].len>w+1){ printf("zu lang %d\n",g.l[i].len); abort(); } }
   for(int i=0;i<g.nf;i++){ if(g.fld[i].form<0||g.fld[i].form>=g.nfrm||g.fld[i].name>=g.pu||g.fld[i].value>=g.pu)abort(); if(g.fld[i].link<0||g.fld[i].link>g.nlk)abort(); }
   for(int i=0;i<g.nlk;i++){ const char*r=g.pool+g.links[i].off; if(r>=g.pool+g.pu)abort(); html_resolve("https://a.example/x/y.html?q=1",r,o,300); }
   free(x); }
 puts("fertig"); }
