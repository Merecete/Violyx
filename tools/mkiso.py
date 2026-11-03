#!/usr/bin/env python3
"""Baut eine ISO mit El Torito fuer BIOS und UEFI (ohne xorriso)."""
import os, struct, sys, time
kernel, lim, conf, out = sys.argv[1:5]
module = sys.argv[5] if len(sys.argv)>5 else None
S=2048
rd=lambda n: open(os.path.join(lim,n),'rb').read()
root={'limine-bios-cd.bin':bytearray(rd('limine-bios-cd.bin')),'limine-uefi-cd.bin':rd('limine-uefi-cd.bin'),'limine-bios.sys':rd('limine-bios.sys'),
      'limine.conf':open(conf,'rb').read(),'boot':dict({'violyx.elf':open(kernel,'rb').read()},**({'install.bin':open(module,'rb').read()} if module else {})),'EFI':{'BOOT':{'BOOTX64.EFI':rd('BOOTX64.EFI')}}}
secs=lambda n: max(1,(n+S-1)//S)
both16=lambda v: struct.pack('<H',v)+struct.pack('>H',v)
both32=lambda v: struct.pack('<I',v)+struct.pack('>I',v)
now=time.gmtime(); stamp=bytes([now.tm_year-1900,now.tm_mon,now.tm_mday,now.tm_hour,now.tm_min,now.tm_sec,0])
def rec(name,lba,size,dir_):
    n=len(name); l=33+n; l+=l%2
    return bytes([l,0])+both32(lba)+both32(size)+stamp+bytes([2 if dir_ else 0,0,0])+both16(1)+bytes([n])+name+(b'\0' if (33+n)%2 else b'')
# Verzeichnisse einsammeln (Breitensuche, fuer die Pfadtabelle)
dirs=[{'name':b'\0','tree':root,'parent':0}]; i=0
while i<len(dirs):
    for k,v in sorted(root.items() if False else dirs[i]['tree'].items()):
        if isinstance(v,dict): dirs.append({'name':k.encode(),'tree':v,'parent':i})
    i+=1
def dsize(d):
    t=34*2
    used=t
    for k in d['tree']:
        n=len(k.encode()); l=33+n; l+=l%2
        if used%S+l>S: used+=S-used%S
        used+=l
    return secs(used)
CAT=19; PL=20; PM=21; lba=22
for d in dirs: d['lba']=lba; d['n']=dsize(d); lba+=d['n']
files=[]
for d in dirs:
    for k,v in sorted(d['tree'].items()):
        if not isinstance(v,dict): files.append((d,k,v,lba)); d.setdefault('f',{})[k]=lba; lba+=secs(len(v))
total=lba
img=bytearray(total*S)
def dirblob(idx):
    d=dirs[idx]; p=dirs[d['parent']]; b=bytearray(rec(b'\0',d['lba'],d['n']*S,True)+rec(b'\1',p['lba'],p['n']*S,True))
    for k,v in sorted(d['tree'].items()):
        if isinstance(v,dict):
            c=[x for x in dirs if x['name']==k.encode() and x['parent']==idx][0]; r=rec(k.encode(),c['lba'],c['n']*S,True)
        else: r=rec(k.encode(),d['f'][k],len(v),False)
        if len(b)%S+len(r)>S: b+=bytes(S-len(b)%S)
        b+=r
    return b
for i,d in enumerate(dirs):
    b=dirblob(i); img[d['lba']*S:d['lba']*S+len(b)]=b
le=be=b''
for i,d in enumerate(dirs):
    n=d['name'] if i else b'\0'; pad=b'\0' if len(n)%2 else b''
    le+=bytes([len(n),0])+struct.pack('<IH',d['lba'],d['parent']+1)+n+pad
    be+=bytes([len(n),0])+struct.pack('>IH',d['lba'],d['parent']+1)+n+pad
img[PL*S:PL*S+len(le)]=le; img[PM*S:PM*S+len(be)]=be
bios_lba=dirs[0]['f']['limine-bios-cd.bin']; efi_lba=dirs[0]['f']['limine-uefi-cd.bin']
bin_=root['limine-bios-cd.bin']; ck=sum(struct.unpack_from('<I',bytes(bin_).ljust((len(bin_)+3)&~3,b'\0'),o)[0] for o in range(64,(len(bin_)+3)&~3,4))&0xFFFFFFFF
struct.pack_into('<IIII',bin_,8,16,bios_lba,len(bin_),ck)
for d,k,v,l in files: img[l*S:l*S+len(v)]=v
pvd=bytearray(S); pvd[0:6]=b'\x01CD001'; pvd[6]=1; pvd[8:40]=b'VIOLYX'.ljust(32); pvd[40:72]=b'VIOLYX'.ljust(32)
pvd[80:88]=both32(total); pvd[120:124]=both16(1); pvd[124:128]=both16(1); pvd[128:132]=both16(S); pvd[132:140]=both32(len(le)); pvd[140:144]=struct.pack('<I',PL); pvd[148:152]=struct.pack('>I',PM)
pvd[156:190]=rec(b'\0',dirs[0]['lba'],dirs[0]['n']*S,True); pvd[190:318]=b' '*128; pvd[318:574]=b' '*256; pvd[574:702]=b' '*128; pvd[702:813]=b' '*111
ts=time.strftime('%Y%m%d%H%M%S00',now).encode()+b'\0'; pvd[813:830]=ts; pvd[830:847]=ts; pvd[847:864]=b'0'*16+b'\0'; pvd[864:881]=b'0'*16+b'\0'; pvd[881]=1
img[16*S:17*S]=pvd
br=bytearray(S); br[0:7]=b'\x00CD001\x01'; br[7:39]=b'EL TORITO SPECIFICATION'.ljust(32,b'\0'); struct.pack_into('<I',br,71,CAT); img[17*S:18*S]=br
term=bytearray(S); term[0:7]=b'\xFFCD001\x01'; img[18*S:19*S]=term
c=bytearray(S); c[0]=1; c[1]=0; c[4:28]=b'VIOLYX'.ljust(24,b'\0'); c[30]=0x55; c[31]=0xAA
w=sum(struct.unpack_from('<H',c,o)[0] for o in range(0,32,2))&0xFFFF; struct.pack_into('<H',c,28,(-w)&0xFFFF)
c[32]=0x88; c[33]=0; struct.pack_into('<HBBHI',c,34,0,0,0,4,bios_lba)
c[64]=0x91; c[65]=0xEF; struct.pack_into('<H',c,66,1); c[68:92]=b'UEFI'.ljust(24,b'\0')
c[96]=0x88; c[97]=0; struct.pack_into('<HBBHI',c,98,0,0,0,min(len(root['limine-uefi-cd.bin'])//512,0xFFFF),efi_lba)
img[CAT*S:CAT*S+S]=c
open(out,'wb').write(img); print(out,len(img)//1024,'KiB')
