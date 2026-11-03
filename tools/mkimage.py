#!/usr/bin/env python3
"""Baut das GPT-Abbild: FAT32-Systempartition mit Limine und Kernel, leere VioFS-Partition."""
import os, struct, sys, zlib, uuid
kernel, limdir, conf, out = sys.argv[1:5]
module = sys.argv[5] if len(sys.argv)>5 else None
SEC=512; ESP_SEC=128*1024; FS_SEC=64*1024; START=2048
TOTAL=START+ESP_SEC+FS_SEC+2048
class Fat:
    def __init__(s,sectors):
        s.spc=1; s.res=32; s.sectors=sectors; n=sectors-s.res
        s.fsz=(n*4+SEC*(1)+2*4)//(SEC+2*4)+1
        s.data=s.res+2*s.fsz; s.clusters=sectors-s.data; assert s.clusters>65525
        s.fat=[0x0FFFFFF8,0xFFFFFFFF,0x0FFFFFFF]; s.cl={}; s.root={}
    def alloc(s,b):
        n=max(1,(len(b)+SEC-1)//SEC); first=len(s.fat)
        for i in range(n): s.fat.append(first+i+1 if i<n-1 else 0x0FFFFFFF)
        s.cl[first]=b.ljust(n*SEC,b'\0'); return first
    def add(s,path,b):
        d=s.root; p=path.strip('/').split('/')
        for x in p[:-1]: d=d.setdefault(x,{})
        d[p[-1]]=b
    def entries(s,tree,parent,me):
        e=b''
        if me is not None:
            e+=struct.pack('<11sBBBHHHHHHHI',b'.          ',0x10,0,0,0,0,0,0,0,0,me&0xFFFF,0) if False else s.raw(b'.          ',0x10,me,0)
            e+=s.raw(b'..         ',0x10,parent or 0,0)
        for i,(name,v) in enumerate(sorted(tree.items())):
            if isinstance(v,dict):
                cl=len(s.fat); s.fat.append(0x0FFFFFFF)   # Verzeichnis belegt einen Cluster
                body=s.entries(v,me,cl); assert len(body)<=SEC
                s.cl[cl]=body.ljust(SEC,b'\0'); size=0; attr=0x10
            else: cl=s.alloc(v); size=len(v); attr=0x20
            b,_,x=name.partition('.'); plain=name==name.upper() and len(b)<=8 and len(x)<=3
            sfn=(b.ljust(8)+x.ljust(3)).encode() if plain else (f'VX{i:06d}'+x[:3].upper().ljust(3)).encode()
            if not plain:
                ck=0
                for c in sfn: ck=((ck>>1)|((ck&1)<<7))+c&255
                u=name.encode('utf-16-le')+b'\0\0'; u=u.ljust((len(u)+25)//26*26,b'\xff'); parts=[u[i:i+26] for i in range(0,len(u),26)]
                for n in range(len(parts),0,-1):
                    q=parts[n-1]; e+=bytes([n|(0x40 if n==len(parts) else 0)])+q[:10]+bytes([0x0F,0,ck])+q[10:22]+b'\0\0'+q[22:26]
            e+=s.raw(sfn,attr,cl,size)
        return e
    def raw(s,n,a,c,z): return n+bytes([a,0,0])+struct.pack('<HHHHHHHI',0,0x5C21,0x5C21,c>>16,0,0x5C21,c&0xFFFF,z)
    def build(s):
        s.fat[2]=0x0FFFFFFF; root=s.entries(s.root,None,None); assert len(root)<=SEC*8
        s.cl[2]=root[:SEC].ljust(SEC,b'\0')
        if len(root)>SEC:
            cur=2
            for i in range(SEC,len(root),SEC):
                n=len(s.fat); s.fat[cur]=n; s.fat.append(0x0FFFFFFF); s.cl[n]=root[i:i+SEC].ljust(SEC,b'\0'); cur=n
        img=bytearray(s.sectors*SEC)
        bpb=bytearray(SEC); bpb[0:3]=b'\xEB\x58\x90'; bpb[3:11]=b'VIOLYX  '
        struct.pack_into('<HBHBHHBHHHII',bpb,11,SEC,s.spc,s.res,2,0,0,0xF8,0,63,255,0,s.sectors)
        struct.pack_into('<IHHIHH',bpb,36,s.fsz,0,0,2,1,6); bpb[64]=0x80; bpb[66]=0x29; struct.pack_into('<I',bpb,67,0x56494F4C); bpb[71:82]=b'VIOLYX     '; bpb[82:90]=b'FAT32   '; bpb[510:512]=b'\x55\xAA'
        img[0:SEC]=bpb; img[6*SEC:7*SEC]=bpb
        fsi=bytearray(SEC); fsi[0:4]=b'RRaA'; fsi[484:488]=b'rrAa'; struct.pack_into('<II',fsi,488,s.clusters-len(s.fat)+2,len(s.fat)); fsi[510:512]=b'\x55\xAA'; img[SEC:2*SEC]=fsi; img[7*SEC:8*SEC]=fsi
        fat=struct.pack(f'<{len(s.fat)}I',*s.fat)
        for k in range(2): img[(s.res+k*s.fsz)*SEC:(s.res+k*s.fsz)*SEC+len(fat)]=fat
        for c,b in s.cl.items():
            o=(s.data+(c-2)*s.spc)*SEC; img[o:o+len(b)]=b
        return img
f=Fat(ESP_SEC)
f.add('/EFI/BOOT/BOOTX64.EFI',open(os.path.join(limdir,'BOOTX64.EFI'),'rb').read())
f.add('/limine-bios.sys',open(os.path.join(limdir,'limine-bios.sys'),'rb').read())
f.add('/limine.conf',open(conf,'rb').read())
f.add('/boot/violyx.elf',open(kernel,'rb').read())
if module: f.add('/boot/install.bin',open(module,'rb').read())
esp=f.build()
img=bytearray(TOTAL*SEC)
img[START*SEC:START*SEC+len(esp)]=esp
fs_lba=START+ESP_SEC
img[fs_lba*SEC:fs_lba*SEC+8]=b'VIOFSNEW'
# GPT
def part(typ,uid,a,b,name): return typ+uid+struct.pack('<QQQ',a,b,0)+name.encode('utf-16-le').ljust(72,b'\0')
ESP=uuid.UUID('C12A7328-F81F-11D2-BA4B-00A0C93EC93B').bytes_le
ents=part(ESP,uuid.uuid4().bytes,START,START+ESP_SEC-1,'EFI System')+part(b'VIOLYXFSPARTITN1',uuid.uuid4().bytes,fs_lba,fs_lba+FS_SEC-1,'Violyx Daten')
ents=ents.ljust(128*128,b'\0'); crc=zlib.crc32(ents)
def hdr(me,alt,table):
    h=bytearray(struct.pack('<8sIIIIQQQQ16sQIII',b'EFI PART',0x10000,92,0,0,me,alt,34,TOTAL-34,uuid.uuid4().bytes,table,128,128,crc))
    struct.pack_into('<I',h,16,zlib.crc32(bytes(h))); return bytes(h).ljust(SEC,b'\0')
img[SEC:2*SEC]=hdr(1,TOTAL-1,2); img[2*SEC:34*SEC]=ents
img[(TOTAL-33)*SEC:(TOTAL-1)*SEC]=ents; img[(TOTAL-1)*SEC:]=hdr(TOTAL-1,1,TOTAL-33)
mbr=bytearray(SEC); mbr[446:462]=bytes([0,0,2,0,0xEE,0xFF,0xFF,0xFF])+struct.pack('<II',1,min(TOTAL-1,0xFFFFFFFF)); mbr[510:512]=b'\x55\xAA'; img[0:SEC]=mbr
open(out,'wb').write(img); print(out,len(img)//1024//1024,'MiB')
