#!/usr/bin/env python3
"""Minimal FAT32 image builder from a directory tree (LFN supported, contiguous files).
usage: mkfat32.py SRC_DIR OUT.img LABEL [extra_MB]"""
import os, sys, struct, math
src, out, label = sys.argv[1], sys.argv[2], sys.argv[3]
extra = int(sys.argv[4]) if len(sys.argv) > 4 else 16
BPS, SPC = 512, 8            # 4 KiB clusters
CL = BPS * SPC
RSV, NFAT = 32, 2

def need_clusters(path):
    n = 1  # this dir
    ents = 2
    for name in os.listdir(path):
        p = os.path.join(path, name)
        ents += 1 + (len(name) + 12) // 13
        if os.path.isdir(p): n += need_clusters(p)
        else: n += max(1, math.ceil(os.path.getsize(p) / CL))
    n += math.ceil(ents * 32 / CL) - 1
    return n
used = need_clusters(src) + 16
clusters = max(used + extra * 1024 * 1024 // CL, 65525 + 16)
fatsz = math.ceil((clusters + 2) * 4 / BPS)
total = RSV + NFAT * fatsz + clusters * SPC
img = open(out, 'wb'); img.truncate(total * BPS)
fat = [0x0FFFFFF8, 0x0FFFFFFF]
data_off = (RSV + NFAT * fatsz) * BPS
def alloc(n):
    start = len(fat)
    for i in range(n): fat.append(start + i + 1 if i < n - 1 else 0x0FFFFFFF)
    return start
def write_cl(c, data):
    img.seek(data_off + (c - 2) * CL); img.write(data)

def short_name(name, used):
    base, ext = (name.rsplit('.', 1) + [''])[:2] if '.' in name else (name, '')
    clean = lambda s: ''.join(ch for ch in s.upper() if ch.isalnum() or ch in '$%\'-_@~`!(){}^#&')
    b, e = clean(base), clean(ext)[:3]
    if name.upper() == (b + ('.' + e if e else '')) and len(b) <= 8 and len(clean(ext)) <= 3 and name == name.upper():
        sn = b.ljust(8) + e.ljust(3)
        if sn not in used: return sn, False
    i = 1
    while True:
        t = '~%d' % i; sn = (b[:8 - len(t)] + t).ljust(8) + e.ljust(3)
        if sn not in used: return sn, True
        i += 1
def lfn_entries(name, sn):
    chk = 0
    for ch in sn.encode(): chk = (((chk & 1) << 7) | (chk >> 1)) + ch & 0xFF
    u = name.encode('utf-16-le') + b'\x00\x00'
    parts = [u[i:i + 26] for i in range(0, len(u), 26)]
    ents = []
    for i, p in enumerate(parts):
        p = p.ljust(26, b'\xff')
        e = bytearray(32); e[0] = (i + 1) | (0x40 if i == len(parts) - 1 else 0)
        e[1:11] = p[0:10]; e[11] = 0x0F; e[13] = chk; e[14:26] = p[10:22]; e[28:32] = p[22:26]
        ents.append(bytes(e))
    return list(reversed(ents))
def dirent(sn, attr, cl, size):
    e = bytearray(32); e[0:11] = sn.encode(); e[11] = attr
    struct.pack_into('<HHHHHHHHI', e, 12, 0, 0, 0x5B5C, 0x5B5C, cl >> 16, 0, 0x5B5C, cl & 0xFFFF, size)
    return bytes(e)

def build(path, my, parent):
    entries = []
    if my is not None:
        entries.append(dirent('.'.ljust(11), 0x10, my, 0))
        entries.append(dirent('..'.ljust(11), 0x10, parent if parent != 2 else 0, 0))
    else:
        entries.append(dirent(label.upper().ljust(11)[:11], 0x08, 0, 0))
    usedn = set()
    for name in sorted(os.listdir(path)):
        p = os.path.join(path, name)
        sn, needlfn = short_name(name, usedn); usedn.add(sn)
        if os.path.isdir(p):
            n_ent = 2 + sum(1 + (len(x) + 12) // 13 for x in os.listdir(p))
            c = alloc(max(1, math.ceil(n_ent * 32 / CL)))
            build(p, c, my if my is not None else 2)
            ent = dirent(sn, 0x10, c, 0)
        else:
            d = open(p, 'rb').read()
            c = alloc(max(1, math.ceil(len(d) / CL))) if d else 0
            if d:
                img.seek(data_off + (c - 2) * CL); img.write(d)
            ent = dirent(sn, 0x20, c, len(d))
        if needlfn or name != name.upper(): entries += lfn_entries(name, sn)
        entries.append(ent)
    blob = b''.join(entries)
    if my is None: return blob
    write_cl(my, blob)

root_n = 1 + sum(1 + (len(x) + 12) // 13 for x in os.listdir(src))
root = alloc(max(1, math.ceil(root_n * 32 / CL)))
blob = build(src, None, None); write_cl(root, blob)
assert len(fat) - 2 <= clusters
bs = bytearray(512); bs[0:3] = b'\xEB\x58\x90'; bs[3:11] = b'MSWIN4.1'
struct.pack_into('<HBHBHHBHHHII', bs, 11, BPS, SPC, RSV, NFAT, 0, 0, 0xF8, 0, 63, 255, 0, total)
struct.pack_into('<IHHIHH12sBBBI11s8s', bs, 36, fatsz, 0, 0, root, 1, 6, b'\0'*12, 0x80, 0, 0x29, 0x2468ACE0, label.upper().ljust(11)[:11].encode(), b'FAT32   ')
bs[510:512] = b'\x55\xAA'
fsi = bytearray(512); struct.pack_into('<I', fsi, 0, 0x41615252); struct.pack_into('<II', fsi, 484, 0x61417272, clusters - (len(fat) - 2)); struct.pack_into('<I', fsi, 492, len(fat)); fsi[510:512] = b'\x55\xAA'
img.seek(0); img.write(bs); img.seek(BPS); img.write(fsi); img.seek(6 * BPS); img.write(bs)
fatb = struct.pack('<%dI' % len(fat), *fat)
for i in range(NFAT): img.seek((RSV + i * fatsz) * BPS); img.write(fatb)
img.close()
print(f'{out}: {total*BPS/1048576:.1f} MiB FAT32, {clusters} clusters, used {len(fat)-2}')
