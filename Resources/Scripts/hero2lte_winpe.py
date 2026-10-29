#!/usr/bin/env python3
"""Build a FAT32 WinPE volume for hero2lte from a Windows ARM64 ISO (or an extracted tree).
usage: hero2lte_winpe.py <Win11_ARM64.iso | extracted_dir> <out.img>
Needs: pycdlib (for ISO input), mkfat32.py next to this script."""
import os, sys, shutil, subprocess, tempfile
src, out = sys.argv[1], sys.argv[2]
here = os.path.dirname(os.path.abspath(__file__))
stage = tempfile.mkdtemp(prefix='winpe_')
WANT = ['efi/', 'boot/boot.sdi', 'boot/bcd', 'sources/boot.wim', 'bootmgr.efi']

def keep(p):
    p = p.lower().lstrip('/')
    return any(p == w or (w.endswith('/') and p.startswith(w)) for w in WANT)

if os.path.isdir(src):
    for root, dirs, files in os.walk(src):
        for f in files:
            full = os.path.join(root, f); rel = os.path.relpath(full, src).replace('\\', '/')
            if keep(rel):
                dst = os.path.join(stage, rel); os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copy2(full, dst)
else:
    import pycdlib
    iso = pycdlib.PyCdlib(); iso.open(src)
    if not iso.has_udf(): sys.exit('ISO has no UDF tree?')
    def walk(path):
        for child in iso.list_children(udf_path=path):
            if child is None or child.is_dot() or child.is_dotdot(): continue
            name = child.file_identifier().decode('utf-8', 'replace')
            p = (path.rstrip('/') + '/' + name)
            if child.is_dir():
                if any(w.startswith(p.lower().lstrip('/') + '/') or p.lower().lstrip('/').startswith(w.rstrip('/')) for w in WANT): walk(p)
            elif keep(p):
                dst = os.path.join(stage, p.lstrip('/')); os.makedirs(os.path.dirname(dst), exist_ok=True)
                print('extract', p); iso.get_file_from_iso(dst, udf_path=p)
    walk('/'); iso.close()

lower = {os.path.relpath(os.path.join(r, f), stage).replace('\\','/').lower() for r, _, fs in os.walk(stage) for f in fs}
for need in ['efi/boot/bootaa64.efi', 'efi/microsoft/boot/bcd', 'boot/boot.sdi', 'sources/boot.wim']:
    if need not in lower: sys.exit('missing ' + need + ' (is this an ARM64 ISO?)')
subprocess.check_call([sys.executable, os.path.join(here, 'mkfat32.py'), stage, out, 'WINPE', '64'])
shutil.rmtree(stage)
print('done ->', out)
