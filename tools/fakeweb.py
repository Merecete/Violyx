#!/usr/bin/env python3
"""Test-Gegenstelle: Router (ARP, DHCP, DNS, ICMP) plus einfaches TCP zu lokalen TLS-Servern.
Aufruf: fakeweb.py PORT LOGDATEI PKI-ORDNER"""
import socket, ssl, struct, sys, threading, importlib.util, time, runpy
port,logf,pki=sys.argv[1:4]
src=open(__file__.replace('fakeweb.py','fakenet.py')).read().split('srv=socket.socket()')[0]
sys.argv=['x',port,logf]; exec(src)
HOSTS={'good.test':(1,9441,'good'),'rsa.test':(2,9442,'rsa'),'wrong.test':(3,9443,'wrong'),'old.test':(4,9444,'old'),'evil.test':(5,9445,'evil'),'tls12.test':(6,9446,'good12'),'redir.test':(7,9447,'redir'),'plain.test':(8,9448,None)}
LAST=b''
PAGE=b'<html><head><title>Violyx Test</title></head><body>Hallo vom Testserver</body></html>\n'
def resp(body,typ='text/html; charset=utf-8',extra=b'',status=b'200 OK'):
    return b'HTTP/1.1 '+status+b'\r\nContent-Type: '+typ.encode()+b'\r\n'+extra+b'Content-Length: %d\r\n\r\n'%len(body)+body
def picture(fmt):
    import io
    from PIL import Image
    im=Image.new('RGB',(320,200))
    px=im.load()
    for y in range(200):
        for x in range(320):
            d=((x-160)**2+(y-100)**2)**.5
            px[x,y]=(255,220,60) if d<50 else (x*255//319,60,y*255//199)
    b=io.BytesIO(); im.save(b,fmt); return b.getvalue()
def browser_paths(path,req):
    import gzip,random
    path=path.split('?')[0]
    if path=='/gz':
        return resp(gzip.compress(b'<title>Gepackt</title><h1>Gzip klappt</h1><p>'+b'Viel Text. '*2000+b'</p>'),extra=b'Content-Encoding: gzip\r\n')
    if path=='/gzkaputt':
        return resp(b'\x1f\x8b\x08\0\0\0\0\0\0\0'+bytes(random.randrange(256) for i in range(500)),extra=b'Content-Encoding: gzip\r\n')
    if path=='/gzbombe':
        return resp(gzip.compress(b'A'*60000000),extra=b'Content-Encoding: gzip\r\n')
    if path=='/b.png': return resp(picture('PNG'),'image/png')
    if path=='/b.jpg': return resp(picture('JPEG'),'image/jpeg')
    if path=='/b.gif': return resp(picture('GIF'),'image/gif')
    if path=='/k.png':
        d=bytearray(picture('PNG'))
        for i in range(60): d[random.randrange(8,len(d))]=random.randrange(256)
        return resp(bytes(d),'image/png')
    if path=='/m.png': return resp(bytes(random.randrange(256) for i in range(4000)),'image/png')
    if path=='/ckset': return resp(b'<title>Keks</title><p>Cookie gesetzt</p><p><a href="/ckget">weiter</a></p>',extra=b'Set-Cookie: sitzung=abc123; Path=/\r\nSet-Cookie: bleibt=xyz789; Max-Age=86400; Path=/\r\nSet-Cookie: fremd=1; Domain=evil.test\r\nSet-Cookie: endung=1; Domain=test\r\n')
    if path=='/ckget':
        c=[l for l in req.split(b'\r\n') if l.lower().startswith(b'cookie:')]
        return resp(b'<title>Keks</title><h1>Cookies</h1><p>ERHALTEN '+(c[0][7:] if c else b'NICHTS')+b' ENDE</p>')
    if path=='/hallo.vpk': return resp(open('/tmp/ft/web_hello.vpk','rb').read(),'application/octet-stream')
    if path=='/daten.bin': return resp(b'BINARYDATA'*5000,'application/octet-stream',extra=b'Content-Disposition: attachment; filename="../../boese name.bin"\r\n')
    if path=='/gross.bin': return resp(b'Z'*(6<<20),'application/zip')
    if path=='/test':
        return resp('''<title>Testseite</title><h1>Hauptuberschrift</h1><p>Text mit <span style="color:#ff4040">roter Schrift</span>, <font color="lime">grun</font>, <span style="color:black">schwarz bleibt lesbar</span> und normal.</p><h2>Tabelle</h2><table><tr><th>Name</th><th>Wert</th><th>Ort</th></tr><tr><td>Alpha</td><td>1</td><td>Berlin</td></tr><tr><td>Beta lang</td><td>22222</td><td>Rom</td></tr><tr><td colspan=2>Summe</td><td>X</td></tr></table><p>Vor dem Bild.</p><img src="/b.png" alt="Sonne PNG"><p>Zwischen den Bildern.</p><a href="/seite"><img src="/b.jpg" alt="Link JPEG"></a><p>Kaputt:</p><img src="/k.png" alt="Bild kaputt"><img src="/m.png" alt="Bild Muell"><img src="/fehlt.png" alt="Bild fehlt"><p>GIF:</p><img src="/b.gif" alt="GIF"><img src="/b.png" width=10 height=10 alt="winzig"><p>Ende der Testseite Suchwort hier.</p>'''.encode())
    return None
def app(req,host):
    path=req.split(b' ')[1].decode(); P('HTTP',req.split(b'\r\n')[0].decode(),[l for l in req.split(b'\r\n') if l.lower().startswith(b'host')])
    if host=='redir': return b'HTTP/1.1 302 Found\r\nLocation: https://good.test/ziel\r\nContent-Length: 0\r\n\r\n'
    if path.startswith('/seite') or path.startswith('/datei'):
        import os
        n=path.split('?')[0].split('#')[0].strip('/').replace('/','_'); f=f'{pki}/web/{n}.html'
        if os.path.exists(f):
            b=open(f,'rb').read(); return b'HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\n\r\n'%len(b)+b
        return b'HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 30\r\n\r\n<h1>404 Nicht gefunden</h1>   '
    if path.startswith('/suche'):
        b=b'<title>Ergebnis</title><h1>Suchergebnis</h1><p>Anfrage: '+LAST.replace(b'<',b' ')+b'</p>'; return b'HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\n\r\n'%len(b)+b
    r=browser_paths(path,req)
    if r: return r
    if path=='/chunk': return b'HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\n\r\n6\r\nHallo \r\n7\r\nchunked\r\n0\r\n\r\n'
    if path=='/gross':
        b=b''.join(b'Zeile %06d mit etwas Inhalt zum Testen des Empfangs\n'%i for i in range(8000)); return b'HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n'%len(b)+b
    return b'HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\n\r\n'%len(PAGE)+PAGE
def serve(p,cert,host):
    s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(('127.0.0.1',p)); s.listen(5); ctx=None
    if cert:
        ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); n=cert.replace('12','').replace('redir','good')
        ctx.load_cert_chain(f'{pki}/{n}.crt',f'{pki}/{n}.key')
        if cert=='good12': ctx.maximum_version=ssl.TLSVersion.TLSv1_2
    while True:
        c0,_=s.accept()
        def h(c0):
            try:
                c=ctx.wrap_socket(c0,server_side=True) if ctx else c0
                P('TLS',cert,c.version() if ctx else 'klar',c.cipher()[0] if ctx else '')
                d=b''
                while b'\r\n\r\n' not in d:
                    x=c.recv(4096)
                    if not x: return
                    d+=x
                hd,_,bd=d.partition(b'\r\n\r\n'); cl=[int(l.split(b':')[1]) for l in hd.split(b'\r\n') if l.lower().startswith(b'content-length')]
                while cl and len(bd)<cl[0]: bd+=c.recv(4096)
                P('BODY',bd[:200].decode(errors='replace')); global LAST; LAST=d.split(b'\r\n')[0]+b' BODY '+bd
                c.sendall(app(d,host)); c.close()
            except Exception as e: P('TLS-FEHLER',cert,repr(e)[:90])
        threading.Thread(target=h,args=(c0,),daemon=True).start()
for n,(i,p,cert) in HOSTS.items(): threading.Thread(target=serve,args=(p,cert,'redir' if n=='redir.test' else n),daemon=True).start()
IP2={bytes([10,9,0,i]):p for _,(i,p,_x) in HOSTS.items()}
conns={}; lock=threading.Lock()
srv=socket.socket(); srv.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); srv.bind(('127.0.0.1',int(port))); srv.listen(1); c,_=srv.accept()
def rawsend(fr):
    with lock: c.sendall(struct.pack('>I',len(fr))+fr)
class Conn:
    def __init__(s,cm,src,dst,sp,dp): s.cm,s.src,s.dst,s.sp,s.dp=cm,src,dst,sp,dp; s.seq=1000; s.ack=0; s.acked=1001; s.done=False; s.sock=socket.create_connection(('127.0.0.1',IP2[dst]))
    def seg(s,fl,pl=b''):
        h=struct.pack('>HHIIBBHHH',s.dp,s.sp,s.seq,s.ack,0x50,fl,65535,0,0); t=h+pl; ckv=ck(s.dst+s.src+struct.pack('>BBH',0,6,len(t))+t); t=t[:16]+struct.pack('>H',ckv)+t[18:]
        rawsend(eth(s.cm,0x800,ip(s.dst,s.src,6,t)))
    def pump(s):
        while True:
            try: d=s.sock.recv(1460)
            except OSError: d=b''
            if not d: break
            while s.seq-s.acked>16000: time.sleep(.002)
            s.seg(0x18,d); s.seq+=len(d)
        s.seg(0x11); s.seq+=1
def tcp(f):
    cm=f[6:12]; p=f[14:]; hl=(p[0]&15)*4; src,dst=p[12:16],p[16:20]; t=p[hl:]; sp,dp,sq,ak,off,fl=struct.unpack('>HHIIBB',t[:14]); pl=p[hl+(off>>4)*4:struct.unpack('>H',p[2:4])[0]]; key=(sp,dst)
    if fl&2:
        if dst not in IP2: return
        k=Conn(cm,src,dst,sp,dp); k.ack=sq+1; conns[key]=k; k.seg(0x12); k.seq+=1; threading.Thread(target=k.pump,daemon=True).start(); return
    k=conns.get(key)
    if not k: return
    if fl&0x10: k.acked=max(k.acked,ak)
    if pl:
        if sq==k.ack: k.ack+=len(pl); k.sock.sendall(pl)
        k.seg(0x10)
    if fl&1:
        k.ack=sq+len(pl)+1; k.seg(0x10)
        try: k.sock.shutdown(socket.SHUT_WR)
        except OSError: pass
_h=handle
def handle2(f,send):
    if struct.unpack('>H',f[12:14])[0]==0x800 and f[14+9]==6: tcp(f); return
    if struct.unpack('>H',f[12:14])[0]==0x800 and f[14+9]==17 and struct.unpack('>H',f[14+(f[14]&15)*4+2:14+(f[14]&15)*4+4])[0]==53:
        p=f[14:]; hl=(p[0]&15)*4; src,dst=p[12:16],p[16:20]; sp=struct.unpack('>H',p[hl:hl+2])[0]; b=p[hl+8:]; i=12; n=[]
        while b[i]: n.append(b[i+1:i+1+b[i]].decode()); i+=b[i]+1
        name='.'.join(n); q=b[12:i+5]; hit=HOSTS.get(name)
        if hit:
            r=b[:2]+b'\x81\x80'+struct.pack('>HHHH',1,1,0,0)+q+b'\xc0\x0c'+struct.pack('>HHIH',1,1,60,4)+bytes([10,9,0,hit[0]]); send(eth(f[6:12],0x800,udp(dst,src,53,sp,r))); return
    _h(f,send)
buf=b''
while True:
    d=c.recv(65536)
    if not d: break
    buf+=d
    while len(buf)>=4:
        n=struct.unpack('>I',buf[:4])[0]
        if len(buf)<4+n: break
        fr=buf[4:4+n]; buf=buf[4+n:]; handle2(fr,rawsend)
