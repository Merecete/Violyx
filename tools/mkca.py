#!/usr/bin/env python3
"""Wandelt PEM-Stammzertifikate in eine C-Tabelle. Aufruf: mkca.py AUSGABE PEM..."""
import base64, re, sys
ders=[]
for f in sys.argv[2:]:
    for m in re.findall(r'-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----',open(f).read(),re.S): ders.append(base64.b64decode(m))
blob=b''.join(ders); off=[]; o=0
for d in ders: off.append(o); o+=len(d)
with open(sys.argv[1],'w') as w:
    w.write(f'#define CA_COUNT {len(ders)}\nstatic const unsigned int ca_off[]={{{",".join(map(str,off))}}};\nstatic const unsigned int ca_len[]={{{",".join(str(len(d)) for d in ders)}}};\nstatic const unsigned char ca_der[]={{\n')
    for i in range(0,len(blob),32): w.write(','.join(map(str,blob[i:i+32]))+',\n')
    w.write('};\n')
print(len(ders),'Stammzertifikate')
