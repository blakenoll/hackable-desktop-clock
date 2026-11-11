import sys, zlib, struct, glob, os
def ppm(p):
    d=open(p,'rb').read(); assert d[:2]==b'P6'; parts=d.split(b'\n',3); return parts[3]
def png(path,w,h,rgb):
    raw=b''.join(b'\0'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw,6))+ch(b'IEND',b''))
names=sys.argv[3:]; cols=int(sys.argv[2]); S=240; pad=8; cell=S+pad
rows=(len(names)+cols-1)//cols; W=cols*cell+pad; H=rows*cell+pad
img=bytearray(b'\x30\x33\x38'*(W*H))
for i,n in enumerate(names):
    px=ppm(f'{n}.ppm'); ox=pad+(i%cols)*cell; oy=pad+(i//cols)*cell
    for y in range(S):
        for x in range(S):
            if (x-119.5)**2+(y-119.5)**2<=120.5**2:
                o=((oy+y)*W+ox+x)*3; s=(y*S+x)*3; img[o:o+3]=px[s:s+3]
png(sys.argv[1],W,H,bytes(img))
