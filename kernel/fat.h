#pragma once
#include "viofs.h"
/* FAT32-Treiber fuer Wechselmedien. Eigenstaendig und auf dem Host testbar. Alle Daten auf dem
   Datentraeger gelten als feindlich: Ketten sind begrenzt, alle Werte werden geprueft. */
typedef struct { vfs_dev d; uint32_t spc,rsvd,nfat,fatsz,root,clusters,datasec,hint,fsinfo; int fsdirty; uint32_t cs; uint8_t buf[512]; int ok; } fatvol;
int fat_mount(fatvol*v,const vfs_dev*d);
int fat_stat(fatvol*v,const char*p,vfs_ent*e);
int64_t fat_read(fatvol*v,const char*p,void*b,uint64_t max);
int fat_write(fatvol*v,const char*p,const void*b,uint64_t len,uint32_t now);
int fat_mkdir(fatvol*v,const char*p,uint32_t now);
int fat_remove(fatvol*v,const char*p);
int fat_rename(fatvol*v,const char*a,const char*b);
int fat_list(fatvol*v,const char*p,int(*cb)(const vfs_ent*,void*),void*arg);
