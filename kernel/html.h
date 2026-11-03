#pragma once
#include <stdint.h>
#include <stddef.h>
/* Eigenstaendiger HTML-Layouter: keine Kernel-Abhaengigkeiten, auch auf dem Host testbar */
enum { HS_NORM,HS_HEAD,HS_BOLD,HS_LINK,HS_DIM,HS_CODE,HS_FIELD };
enum { HF_TEXT,HF_SUBMIT,HF_HIDDEN };
typedef struct { uint32_t off; uint16_t len; } hline;
typedef struct { uint32_t h; int line; } hanchor;
typedef struct { uint32_t off; int line; int field; } hlink;
typedef struct { uint32_t name,value; int form; int type; int link; int cols; } hfield;
typedef struct { uint32_t action; int post; } hform;
typedef struct {
  hline *l; int nl,maxl;
  uint8_t *ch,*st; uint16_t *lk; uint32_t used,cap;
  char *pool; uint32_t pu,pc;
  hlink *links; int nlk,maxlk;
  hanchor *anch; int nan,maxan;
  hfield *fld; int nf,maxf;
  hform *frm; int nfrm,maxfrm;
  int nums;
  char title[80]; int full;
} hpage;
void html_layout(const char *src,size_t len,int width,int latin1,int plain,hpage *p);
int html_resolve(const char *base,const char *rel,char *out,size_t n);
extern int html_lang;
uint32_t html_hash(const char *s);
uint16_t html_cp437_uni(uint8_t c);
