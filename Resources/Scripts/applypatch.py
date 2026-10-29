import sys,struct
# usage: applypatch.py file patch   (in place)
p=open(sys.argv[2],'rb'); assert p.read(4)==b'WPAT'
size,n=struct.unpack('<QI',p.read(12))
with open(sys.argv[1],'r+b') as f:
    for _ in range(n):
        s,l=struct.unpack('<QQ',p.read(16)); f.seek(s); f.write(p.read(l))
    f.truncate(size)
print('patched',sys.argv[1],size)
