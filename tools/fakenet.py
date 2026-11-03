#!/usr/bin/env python3
"""Simuliertes Netz fuer Tests: Router mit DHCP, ARP, DNS und ICMP an QEMUs Socket-Netdev.
Aufruf: fakenet.py PORT LOGDATEI"""
import socket, struct, sys
MAC=bytes.fromhex('525400000002'); GW=bytes([10,0,2,2]); DNS=bytes([10,0,2,3]); CL=bytes([10,0,2,15]); MASK=bytes([255,255,255,0]); DEAD=bytes([10,0,2,99])
log=open(sys.argv[2],'a',buffering=1); P=lambda *a: print(*a,file=log)
def ck(b):
    if len(b)%2: b+=b'\0'
    s=sum(struct.unpack(f'>{len(b)//2}H',b))
    while s>>16: s=(s&0xFFFF)+(s>>16)
    return ~s&0xFFFF
def ip(src,dst,proto,pl):
    h=bytearray(struct.pack('>BBHHHBBH4s4s',0x45,0,20+len(pl),1,0,64,proto,0,src,dst)); struct.pack_into('>H',h,10,ck(bytes(h))); return bytes(h)+pl
def udp(src,dst,sp,dp,pl):
    u=struct.pack('>HHHH',sp,dp,8+len(pl),0)+pl; c=ck(src+dst+struct.pack('>BBH',0,17,len(u))+u) or 0xFFFF; return ip(src,dst,17,u[:6]+struct.pack('>H',c)+pl)
def eth(dst,t,pl): return dst+MAC+struct.pack('>H',t)+pl
def dhcp(f,x,kind,cm):
    b=struct.pack('>BBBBIHH4s4s4s4s',2,1,6,0,x,0,0,bytes(4),CL,GW,bytes(4))+cm.ljust(16,b'\0')+bytes(192)+b'\x63\x82\x53\x63'
    b+=bytes([53,1,kind,54,4])+GW+bytes([51,4,0,1,81,128,1,4])+MASK+bytes([3,4])+GW+bytes([6,4])+DNS+bytes([255]); return eth(b'\xff'*6,0x800,udp(GW,b'\xff'*4,67,68,b))
def handle(f,send):
    d,s,t=f[:6],f[6:12],struct.unpack('>H',f[12:14])[0]; p=f[14:]
    if t==0x806:
        if p[6:8]==b'\0\1':
            tip=p[24:28]; spa=p[14:18]; P('ARP',spa.hex(),'fragt',tip.hex())
            if tip in (GW,DNS): send(eth(s,0x806,bytes.fromhex('0001080006040002')+MAC+tip+s+spa))
    elif t==0x800:
        pr=p[9]; hl=(p[0]&15)*4; src,dst=p[12:16],p[16:20]; pl=p[hl:]
        if pr==17:
            sp,dp=struct.unpack('>HH',pl[:4]); b=pl[8:]
            if dp==67:
                x=struct.unpack('>I',b[4:8])[0]; o=b[240:]; kind=0
                while o and o[0]!=255:
                    if o[0]==53: kind=o[2]
                    if o[0]==0: o=o[1:]; continue
                    o=o[2+o[1]:]
                P('DHCP',kind); send(dhcp(f,x,2 if kind==1 else 5,s))
            elif dp==53:
                i=12; n=[]
                while b[i]: n.append(b[i+1:i+1+b[i]].decode()); i+=b[i]+1
                name='.'.join(n); P('DNS',name); q=b[12:i+5]; ok=not name.endswith('invalid')
                r=b[:2]+(b'\x81\x80' if ok else b'\x81\x83')+struct.pack('>HHHH',1,1 if ok else 0,0,0)+q
                if ok: r+=b'\xc0\x0c'+struct.pack('>HHIH',1,1,60,4)+bytes([93,184,216,34])
                send(eth(s,0x800,udp(dst,src,53,sp,r)))
        elif pr==1 and pl[0]==8:
            P('PING',dst.hex(),'ok' if ck(pl)==0 else 'Pruefsumme falsch')
            if dst!=DEAD: r=bytearray(b'\0\0\0\0'+pl[4:]); struct.pack_into('>H',r,2,ck(bytes(r))); send(eth(s,0x800,ip(dst,src,1,bytes(r))))
srv=socket.socket(); srv.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); srv.bind(('127.0.0.1',int(sys.argv[1]))); srv.listen(1)
c,_=srv.accept(); buf=b''
def send(fr): c.sendall(struct.pack('>I',len(fr))+fr)
while True:
    d=c.recv(65536)
    if not d: break
    buf+=d
    while len(buf)>=4:
        n=struct.unpack('>I',buf[:4])[0]
        if len(buf)<4+n: break
        fr=buf[4:4+n]; buf=buf[4+n:]; handle(fr,send)
