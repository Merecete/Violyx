#!/usr/bin/env python3
"""Verpackt das Grundabbild kompakt fuer den Installer.
Format: "VIOINST1", u64 Sektoren, u32 Datensaetze, u32 0, dann je (u64 lba, u32 anzahl, u32 art), dann die Daten.
Art 0 = Bereich nullen (FAT und Kopf der Datenpartition), Art 1 = Daten."""
import struct, sys
src,out=sys.argv[1:3]; d=open(src,'rb').read(); S=512; n=len(d)//S
START=2048; ESP=128*1024; FS=START+ESP
recs=[(START,8192,0,b''),(FS,8,0,b'')]; i=0; blobs=[]
while i<n:
    if d[i*S:(i+1)*S].strip(b'\0')==b'': i+=1; continue
    j=i
    while j<n and j-i<4096 and d[j*S:(j+1)*S].strip(b'\0')!=b'': j+=1
    recs.append((i,j-i,1,d[i*S:j*S])); i=j
with open(out,'wb') as o:
    o.write(b'VIOINST1'+struct.pack('<QII',n,len(recs),0))
    for l,c,k,_ in recs: o.write(struct.pack('<QII',l,c,k))
    for r in recs: o.write(r[3])
print(out,sum(r[1] for r in recs if r[2])*S//1024,'KiB Daten in',len(recs),'Abschnitten')
