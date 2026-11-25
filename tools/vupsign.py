#!/usr/bin/env python3
"""Violyx updates: keys, update files and the online update list. Plain Python, nothing to install.

  vupsign.py keygen KEYFILE              new key: the secret one into KEYFILE (keep it safe, never
                                         publish it), the public one into system/update_key.h
  vupsign.py pack KEYFILE OUTDIR [NOTES] signs build/violyx.elf (and build/install.bin) as
                                         OUTDIR/violyx-VERSION.vup and writes OUTDIR/update.txt
  vupsign.py check FILE                  checks the signature of a .vup or update.txt

NOTES is the release notes file: its "What's new" part is shown before updating.
Signature: ECDSA on the curve P-256 over SHA-256, deterministic (RFC 6979), r and s as 64 bytes.
"""
import hashlib
import hmac
import os
import re
import secrets
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
     0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)


def add(p, q):
    if p is None:
        return q
    if q is None:
        return p
    if p[0] == q[0] and (p[1] + q[1]) % P == 0:
        return None
    if p == q:
        l = (3 * p[0] * p[0] + A) * pow(2 * p[1], P - 2, P) % P
    else:
        l = (q[1] - p[1]) * pow(q[0] - p[0], P - 2, P) % P
    x = (l * l - p[0] - q[0]) % P
    return (x, (l * (p[0] - x) - p[1]) % P)


def mul(k, p):
    r = None
    while k:
        if k & 1:
            r = add(r, p)
        p = add(p, p)
        k >>= 1
    return r


def b32(i):
    return i.to_bytes(32, "big")


def rfc6979(d, h):
    v, k = b"\x01" * 32, b"\x00" * 32
    x, z = b32(d), b32(int.from_bytes(h, "big") % N)
    k = hmac.new(k, v + b"\x00" + x + z, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    k = hmac.new(k, v + b"\x01" + x + z, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    while True:
        v = hmac.new(k, v, hashlib.sha256).digest()
        t = int.from_bytes(v, "big")
        if 1 <= t < N:
            return t
        k = hmac.new(k, v + b"\x00", hashlib.sha256).digest()
        v = hmac.new(k, v, hashlib.sha256).digest()


def sign(d, msg):
    h = hashlib.sha256(msg).digest()
    z = int.from_bytes(h, "big")
    while True:
        k = rfc6979(d, h)
        r = mul(k, G)[0] % N
        s = pow(k, N - 2, N) * (z + r * d) % N
        if r and s:
            return b32(r) + b32(s)


def verify(q, msg, sig):
    r, s = int.from_bytes(sig[:32], "big"), int.from_bytes(sig[32:], "big")
    if not (1 <= r < N and 1 <= s < N):
        return False
    z = int.from_bytes(hashlib.sha256(msg).digest(), "big")
    w = pow(s, N - 2, N)
    pt = add(mul(z * w % N, G), mul(r * w % N, q))
    return pt is not None and pt[0] % N == r


def load_key(path):
    t = open(path).read().split()
    if len(t) < 2 or t[0] != "VIOLYX-UPDATE-KEY":
        sys.exit("not a Violyx update key: " + path)
    d = int(t[1], 16)
    return d, mul(d, G)


def pub_bytes(q):
    return b"\x04" + b32(q[0]) + b32(q[1])


def header_pub():
    """the public key the system has (system/update_key.h)"""
    t = open(os.path.join(ROOT, "system", "update_key.h")).read()
    b = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", t))
    if len(b) != 65:
        sys.exit("system/update_key.h has no key")
    return (int.from_bytes(b[1:33], "big"), int.from_bytes(b[33:], "big"))


def keygen(path):
    if os.path.exists(path):
        sys.exit(path + " exists already, it is not overwritten")
    d = secrets.randbelow(N - 1) + 1
    q = mul(d, G)
    with open(path, "w") as f:
        f.write("VIOLYX-UPDATE-KEY\n%064x\n" % d)
    os.chmod(path, 0o600)
    b = pub_bytes(q)
    rows = ",\n".join("    " + ", ".join("0x%02X" % c for c in b[i:i + 13]) for i in range(0, 65, 13))
    with open(os.path.join(ROOT, "system", "update_key.h"), "w") as f:
        f.write("#pragma once\n/* Public key for updates (P-256, uncompressed). Only updates signed with "
                "the secret key\n * that belongs to it are accepted (tools/vupsign.py). */\n"
                "static const unsigned char update_pub[65] = {\n" + rows + "};\n")
    print("secret key:", path, "(keep it safe and never publish it)")
    print("public key: system/update_key.h")


def notes_of(path):
    """the "What's new" part of the release notes, without markdown"""
    if not path:
        return []
    t = open(path, encoding="utf-8").read()
    m = re.search(r"## What's new\s*\n(.*?)(\n## |\Z)", t, re.S)
    out = []
    for l in (m.group(1) if m else "").strip().splitlines():
        l = l.replace("**", "").replace("`", "").rstrip()
        if l.strip() and l.strip() != "end":
            out.append(l)
    return out


def pack(keyfile, outdir, notes=None):
    d, q = load_key(keyfile)
    if pub_bytes(q) != pub_bytes(header_pub()):
        sys.exit("this key does not belong to system/update_key.h")
    ver = re.search(r'"(.*)"', open(os.path.join(ROOT, "kernel", "version.h")).read()).group(1)
    files = [("boot/violyx.elf", os.path.join(ROOT, "build", "violyx.elf"))]
    ins = os.path.join(ROOT, "build", "install.bin")
    if os.path.exists(ins):
        files.append(("boot/install.bin", ins))
    data = [open(p, "rb").read() for _, p in files]
    nl = notes_of(notes)
    h = "VUP1\nversion %s\n" % ver
    for (n, _), b in zip(files, data):
        h += "file %s %d\n" % (n, len(b))
    h += "notes\n" + "".join(l + "\n" for l in nl) + "end\n"
    body = h.encode() + b"".join(data)
    vup = body + sign(d, body)
    os.makedirs(outdir, exist_ok=True)
    name = "violyx-%s.vup" % ver
    open(os.path.join(outdir, name), "wb").write(vup)
    m = "violyx-update 1\nversion %s\nfile %s\nsize %d\nsha256 %s\nnotes\n" % (
        ver, name, len(vup), hashlib.sha256(vup).hexdigest())
    m += "".join(l + "\n" for l in nl) + "end\n"
    m = m.encode()
    m += b"sig " + sign(d, m).hex().encode() + b"\n"
    open(os.path.join(outdir, "update.txt"), "wb").write(m)
    print("%s/%s (%d bytes) and %s/update.txt" % (outdir, name, len(vup), outdir))


def check(path):
    q = header_pub()
    b = open(path, "rb").read()
    if b.startswith(b"VUP1\n"):
        ok = verify(q, b[:-64], b[-64:])
    else:
        i = b.rfind(b"sig ")
        ok = i > 0 and verify(q, b[:i], bytes.fromhex(b[i + 4:].strip().decode()))
    print(path, "signature ok" if ok else "SIGNATURE WRONG")
    return ok


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == "keygen":
        keygen(a[1])
    elif len(a) >= 3 and a[0] == "pack":
        pack(a[1], a[2], a[3] if len(a) > 3 else None)
    elif len(a) >= 2 and a[0] == "check":
        sys.exit(0 if check(a[1]) else 1)
    else:
        sys.exit(__doc__)
