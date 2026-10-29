import sys,struct
a=open(sys.argv[1],'rb').read(); b=open(sys.argv[2],'rb').read()
B=1<<16; ranges=[]
n=min(len(a),len(b))
i=0
while i<n:
    if a[i:i+B]!=b[i:i+B]:
        j=i
        while j<n and a[j:j+B]!=b[j:j+B]: j+=B
        ranges.append((i,min(j,n)))
        i=j
    else: i+=B
if len(b)>n:
    if ranges and ranges[-1][1]>=n: ranges[-1]=(ranges[-1][0],len(b))
    else: ranges.append((n,len(b)))
with open(sys.argv[3],'wb') as f:
    f.write(b'WPAT'+struct.pack('<QI',len(b),len(ranges)))
    for s,e in ranges: f.write(struct.pack('<QQ',s,e-s)); f.write(b[s:e])
print(ranges[:5],len(ranges),sum(e-s for s,e in ranges))
