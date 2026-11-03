#!/usr/bin/env python3
"""Startet Violyx in QEMU, tippt per Monitor und speichert Bildschirmfotos."""
import socket, subprocess, sys, time, os
from PIL import Image
QEMU = os.environ.get('QEMU', '/root/tools/qemu/build64/qemu-system-x86_64')
FW = os.environ.get('VIOLYX_FW', '/root/tools/fw/code.fd')
BIOS = os.environ.get('QEMU_BIOS', '/root/tools/qemu/pc-bios')
class VM:
    def __init__(s, img, name='vm', fw='bios', disk='ide', usb=False, mem='256', extra=()):
        s.path = f'/tmp/violyx-{name}.sock'; s.dbg = f'/tmp/violyx-{name}.dbg'
        if os.path.exists(s.path): os.unlink(s.path)
        cmd = [QEMU, '-m', mem, '-display', 'none', '-monitor', f'unix:{s.path},server=on,wait=off', '-no-reboot', '-debugcon', f'file:{s.dbg}', '-L', BIOS]
        cmd += list(extra)
        if fw == 'uefi': cmd += ['-drive', 'if=pflash,format=raw,readonly=on,file=' + FW]
        if disk == 'ide': cmd += ['-drive', f'file={img},format=raw,if=ide']
        elif disk == 'ahci': cmd += ['-machine', 'q35', '-drive', f'file={img},format=raw,if=none,id=d0', '-device', 'ide-hd,drive=d0,bus=ide.0']
        elif disk == 'nvme': cmd += ['-machine', 'q35', '-drive', f'file={img},format=raw,if=none,id=d0', '-device', 'nvme,drive=d0,serial=VIOLYX1,bootindex=0']
        elif disk == 'cd': cmd += ['-cdrom', img, '-boot', 'd']
        elif disk == 'vdi': cmd += ['-machine', 'q35', '-drive', f'file={img},format=vdi,if=none,id=d0', '-device', 'ide-hd,drive=d0,bus=ide.0']
        elif disk == 'usb': cmd += ['-drive', f'file={img},format=raw,if=none,id=d0', '-device', 'qemu-xhci,id=x', '-device', 'usb-kbd,bus=x.0', '-device', 'usb-storage,bus=x.0,drive=d0,bootindex=0']
        if usb: cmd += ['-device', 'qemu-xhci,id=x', '-device', 'usb-kbd,bus=x.0']
        s.p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL)
        for _ in range(100):
            if os.path.exists(s.path): break
            time.sleep(.1)
        s.c = socket.socket(socket.AF_UNIX); s.c.connect(s.path); s.c.settimeout(.02); s.drain()
    def drain(s):
        d = b''
        try:
            while True:
                x = s.c.recv(65536)
                if not x: break
                d += x
        except socket.timeout: pass
        return d.decode(errors='replace')
    def cmd(s, c):
        s.c.sendall((c + '\n').encode()); time.sleep(.03); return s.drain()
    def key(s, *ks):
        for k in ks: s.cmd('sendkey ' + k + ' 25'); time.sleep(.04)
    def type(s, t):
        m = {' ': 'spc', '.': 'dot', '\n': 'ret', 'z': 'y', 'y': 'z', ',': 'comma', '-': 'slash', '+': 'bracket_right',
             '*': 'shift-bracket_right', '(': 'shift-8', ')': 'shift-9', '/': 'shift-7', 'ä': 'apostrophe', 'ö': 'semicolon', 'ü': 'bracket_left', 'ß': 'minus', '!': 'shift-1', '%': 'shift-5', ':': 'shift-dot', ';': 'shift-comma', '_': 'shift-slash', '?': 'shift-minus', '=': 'shift-0', '@': 'alt_r-q', '{': 'alt_r-7', '[': 'alt_r-8', ']': 'alt_r-9', '}': 'alt_r-0', '\\': 'alt_r-minus', '~': 'alt_r-bracket_right', '<': 'less', '>': 'shift-less', '|': 'alt_r-less', '#': 'backslash', 'Ä': 'shift-apostrophe', 'Ö': 'shift-semicolon', 'Ü': 'shift-bracket_left'}
        for c in t:
            k = m.get(c)
            if k is None and c.isupper():
                l = c.lower(); k = 'shift-' + m.get(l, l)
            if k is None: k = c
            s.cmd('sendkey ' + k + ' 25'); time.sleep(.04)
    def shot(s, f):
        time.sleep(.4); p = f + '.ppm'; s.cmd(f'screendump {p}'); time.sleep(.5)
        Image.open(p).save(f); os.unlink(p)
    def quit(s):
        s.c.sendall(b'quit\n'); s.p.wait(10)
