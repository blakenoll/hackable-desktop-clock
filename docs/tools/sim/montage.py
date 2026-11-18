# Usage: montage.py [--bezel] out.png columns name...   (reads <name>.ppm from the current directory)
# Plain: faces on a flat grey grid, no frame. Cropped by the web flasher, whose CSS draws the bezel, so the geometry is fixed.
# --bezel: the README look. Faces in the flasher's brushed bezel, on a transparent background.
import sys, zlib, struct, math
def ppm(p):
    d=open(p,'rb').read(); assert d[:2]==b'P6'; parts=d.split(b'\n',3); return parts[3]
def png(path,w,h,px,ctype=2):  # ctype 2 = RGB, 6 = RGBA
    n=4 if ctype==6 else 3; raw=b''.join(b'\0'+px[y*w*n:(y+1)*w*n] for y in range(h))
    def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,ctype,0,0,0))+ch(b'IDAT',zlib.compress(raw,6))+ch(b'IEND',b''))
args=sys.argv[1:]; bezel=args[0]=='--bezel'
if bezel: args=args[1:]
out=args[0]; cols=int(args[1]); names=args[2:]; S=240
rows=(len(names)+cols-1)//cols
if not bezel:
    pad=8; cell=S+pad; W=cols*cell+pad; H=rows*cell+pad
    img=bytearray(b'\x30\x33\x38'*(W*H))
    for i,n in enumerate(names):
        px=ppm(f'{n}.ppm'); ox=pad+(i%cols)*cell; oy=pad+(i//cols)*cell
        for y in range(S):
            for x in range(S):
                if (x-119.5)**2+(y-119.5)**2<=120.5**2:
                    o=((oy+y)*W+ox+x)*3; s=(y*S+x)*3; img[o:o+3]=px[s:s+3]
    png(out,W,H,bytes(img)); sys.exit()
# Transparent background (RGBA), so the README images sit on GitHub's light or dark page.
# Bezel colours from site/index.html: linear-gradient(170deg, #4a5059, #2a2e34 38%, #15171a 72%, #23262b).
RING=9; R=S//2+RING; gap=22; cell=2*R+gap; W=cols*cell+gap; H=rows*cell+gap
STOPS=[(0,(0x4a,0x50,0x59)),(.38,(0x2a,0x2e,0x34)),(.72,(0x15,0x17,0x1a)),(1,(0x23,0x26,0x2b))]
def grad(t):
    t=min(1,max(0,t))
    for (a,ca),(b,cb) in zip(STOPS,STOPS[1:]):
        if t<=b: f=(t-a)/(b-a); return tuple(ca[k]+(cb[k]-ca[k])*f for k in range(3))
    return STOPS[-1][1]
img=bytearray(W*H*4)
s=math.sin(math.radians(170)); co=-math.cos(math.radians(170)); L=(abs(2*R*s)+abs(2*R*co))
for i,n in enumerate(names):
    px=ppm(f'{n}.ppm'); cx0=gap+(i%cols)*cell+R; cy0=gap+(i//cols)*cell+R
    for y in range(-R-4,R+4):
        for x in range(-R-4,R+4):
            d=math.hypot(x+.5,y+.5); o=((cy0+y)*W+cx0+x)*4
            if d<=R:
                if d<=S/2:  # glass: the rendered face (inner 1px stays black like the CSS inset)
                    fx,fy=x+S//2,y+S//2
                    if 0<=fx<S and 0<=fy<S: img[o:o+4]=px[(fy*S+fx)*3:(fy*S+fx)*3+3]+b'\xff'
                else:  # bezel, with a 1px anti-aliased rim in alpha
                    c=grad(.5+(x*s+y*co)/L); a=min(1,R-d) if d>R-1 else 1
                    img[o:o+4]=bytes(int(v) for v in c)+bytes([round(255*a)])
            elif d<=R+4:  # soft drop shadow: black, fading out
                img[o:o+4]=bytes([0,0,0,round(255*(1-(d-R)/4)*.5)])
png(out,W,H,bytes(img),6)
