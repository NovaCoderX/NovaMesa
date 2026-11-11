import sys
from PIL import Image, ImageChops
a=Image.open(sys.argv[1]).convert('RGB'); b=Image.open(sys.argv[2]).convert('RGB')
d=ImageChops.difference(a,b); px=list(d.getdata()); n=len(px)
nz=sum(1 for p in px if p!=(0,0,0)); big=sum(1 for p in px if max(p)>24); mx=max(max(p) for p in px)
print(f"{sys.argv[1].split('/')[-1]:28s} vs {sys.argv[2].split('/')[-1]:28s} differing px {100.0*nz/n:6.2f}%  >24: {100.0*big/n:6.2f}%  max {mx}")
