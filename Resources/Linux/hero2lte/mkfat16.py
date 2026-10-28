#!/usr/bin/env python3
"""Tiny FAT16 image builder: root-directory files only, 8.3 names, contiguous clusters.
usage: mkfat16.py out.img size_mb LABEL NAME.EXT=path [...]"""
import sys, struct, math
out, size_mb, label = sys.argv[1], int(sys.argv[2]), sys.argv[3]
files = [a.split('=', 1) for a in sys.argv[4:]]
BPS, SPC, RSV, NFAT, ROOTENT = 512, 4, 4, 2, 512
total = size_mb * 1024 * 1024 // BPS
root_secs = ROOTENT * 32 // BPS
# FAT size: iterate
fatsz = 1
while True:
    data = total - RSV - NFAT * fatsz - root_secs
    clusters = data // SPC
    need = math.ceil((clusters + 2) * 2 / BPS)
    if need <= fatsz: break
    fatsz = need
assert 4085 <= clusters < 65525, clusters
img = bytearray(total * BPS)
bs = bytearray(512)
bs[0:3] = b'\xEB\x3C\x90'; bs[3:11] = b'MSWIN4.1'
struct.pack_into('<HBHBHHBHHHII', bs, 11, BPS, SPC, RSV, NFAT, ROOTENT,
                 total if total < 65536 else 0, 0xF8, fatsz, 63, 255, 0, total if total >= 65536 else 0)
struct.pack_into('<BBBI11s8s', bs, 36, 0x80, 0, 0x29, 0x12345678, label.ljust(11)[:11].encode(), b'FAT16   ')
bs[510:512] = b'\x55\xAA'
img[0:512] = bs
fat = bytearray(fatsz * BPS)
struct.pack_into('<HH', fat, 0, 0xFFF8, 0xFFFF)
root = bytearray(root_secs * BPS)
# volume label entry
root[0:11] = label.ljust(11)[:11].encode(); root[11] = 0x08
data_start = RSV + NFAT * fatsz + root_secs
cl = 2; ent = 1
for name, path in files:
    d = open(path, 'rb').read()
    base, _, ext = name.upper().partition('.')
    n83 = base.ljust(8)[:8] + ext.ljust(3)[:3]
    ncl = max(1, math.ceil(len(d) / (SPC * BPS)))
    first = cl if d else 0
    if d:
        for i in range(ncl):
            struct.pack_into('<H', fat, (cl + i) * 2, 0xFFFF if i == ncl - 1 else cl + i + 1)
        off = (data_start + (cl - 2) * SPC) * BPS
        img[off:off + len(d)] = d
        cl += ncl
    e = bytearray(32); e[0:11] = n83.encode(); e[11] = 0x20
    # ctime(14) cdate(16) adate(18) clus_hi(20) mtime(22) mdate(24) clus_lo(26) size(28)
    struct.pack_into('<HHHHHHHI', e, 14, 0, 0x5B5C, 0x5B5C, 0, 0, 0x5B5C, first, len(d))
    root[ent * 32:(ent + 1) * 32] = e; ent += 1
    assert cl - 2 <= clusters, 'image too small'
for i in range(NFAT):
    o = (RSV + i * fatsz) * BPS; img[o:o + len(fat)] = fat
o = (RSV + NFAT * fatsz) * BPS; img[o:o + len(root)] = root
open(out, 'wb').write(img)
print(f'{out}: {size_mb} MB FAT16, {clusters} clusters, used {cl-2}')
