#!/usr/bin/env python3
"""Unabhaengige FAT32-Pruefung: FAT-Kopien, Ketten, Ueberschneidungen, Pruefsummen, verlorene Cluster."""
import struct, sys
d=open(sys.argv[1],'rb').read(); S=512
spc=d[13]; rs=struct.unpack('<H',d[14:16])[0]; nf=d[16]; tot=struct.unpack('<I',d[32:36])[0]; fz=struct.unpack('<I',d[36:40])[0]; root=struct.unpack('<I',d[44:48])[0]
ds=rs+nf*fz; cl=(tot-ds)//spc; fat=d[rs*S:(rs+fz)*S]; err=[]
if nf==2 and d[(rs+fz)*S:(rs+2*fz)*S]!=fat: err.append('FAT-Kopien verschieden')
F=lambda c: struct.unpack_from('<I',fat,c*4)[0]&0x0FFFFFFF
owner={}; files=0; dirs=0; lost=0
def chain(c,who):
    o=[]
    while 2<=c<cl+2:
        if c in owner: err.append(f'Cluster {c} doppelt: {who} und {owner[c]}'); return o
        if F(c)==0: err.append(f'Cluster {c} frei, aber in Kette {who}'); return o
        owner[c]=who; o.append(c); n=F(c)
        if n>=0x0FFFFFF8: break
        c=n
    return o
def sec(c): return ds+(c-2)*spc
names=[]
def scan(c,path):
    global files,dirs
    ch=chain(c,path); data=b''.join(d[sec(x)*S:(sec(x)+spc)*S] for x in ch); seen=set(); lfn=[]; ck=None
    for o in range(0,len(data),32):
        e=data[o:o+32]
        if e[0]==0: break
        if e[0]==0xE5: lfn=[]; continue
        if (e[11]&0x3F)==0x0F: lfn.append(e); ck=e[13]; continue
        if e[11]&8: lfn=[]; continue
        sn=e[:11]; s=0
        for b in sn: s=(((s&1)<<7)+(s>>1)+b)&255
        if lfn and s!=ck: err.append(f'LFN-Pruefsumme falsch in {path}: {sn}')
        if lfn:
            seqs=[x[0]&0x1F for x in lfn]
            if seqs!=list(range(len(lfn),0,-1)) or not lfn[0][0]&0x40: err.append(f'LFN-Reihenfolge {path} {sn} {seqs}')
        name=''
        for x in reversed(lfn):
            name+=(x[1:11]+x[14:26]+x[28:32]).decode('utf-16-le','replace')
        name=name.split('\0')[0].rstrip('￿') if lfn else sn.decode('cp437')
        if sn in seen: err.append(f'Kurzname doppelt {path} {sn}')
        seen.add(sn); lfn=[]
        if sn[:2]==b'. ' or sn[:3]==b'.. ': continue
        cl_=struct.unpack('<H',e[20:22])[0]<<16|struct.unpack('<H',e[26:28])[0]; sz=struct.unpack('<I',e[28:32])[0]; names.append(path+'/'+name)
        if e[11]&0x10:
            dirs+=1
            if cl_: scan(cl_,path+'/'+name)
        else:
            files+=1
            n=len(chain(cl_,path+'/'+name)) if cl_ else 0
            if n*spc*S<sz or (sz and (n-1)*spc*S>=sz) or (sz==0 and cl_): err.append(f'Groesse passt nicht zur Kette: {path}/{name} {sz} {n}')
scan(root,'')
for c in range(2,cl+2):
    if F(c)!=0 and c not in owner: lost+=1
print(f'{files} Dateien, {dirs} Ordner, {lost} verlorene Cluster, {len(err)} Fehler')
for e in err[:12]: print(' ',e)
sys.exit(1 if err else 0)
