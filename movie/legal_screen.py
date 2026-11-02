# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the start-up notice picture (640x480, all original artwork) -> legal_screen.png
needs: python3, numpy, opencv-python, pillow"""
import os, cv2, numpy as np
os.chdir(os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFont
W,H=640,480
rng=np.random.default_rng(3)
# --- starfield background (original artwork) ---
yy,xx=np.mgrid[0:H,0:W].astype(np.float32)
bg=np.zeros((H,W,3),np.float32)
top=np.array([6,8,16],np.float32); mid=np.array([10,14,28],np.float32)
t=(yy/H)[...,None]; bg=top*(1-t)+mid*t
# soft nebula clouds
for sc,amp,col in ((90,1.0,(26,34,66)),(40,0.6,(22,30,56))):
    n=cv2.GaussianBlur(rng.random((H,W)).astype(np.float32),(0,0),sc); n=(n-n.min())/(n.max()-n.min())
    n=np.clip((n-0.45)*2.2,0,1)[...,None]
    bg+=n*np.array(col,np.float32)*amp*0.55
# horizon glow, bottom edge
g=np.exp(-((H-yy)/70.0)**2)[...,None]*np.array([34,46,72],np.float32)
bg+=g*0.9
# stars
img=np.clip(bg,0,255)
def star(x,y,b,r):
    x0,x1=int(max(0,x-6*r-2)),int(min(W,x+6*r+3)); y0,y1=int(max(0,y-6*r-2)),int(min(H,y+6*r+3))
    sy,sx=np.mgrid[y0:y1,x0:x1].astype(np.float32)
    d2=(sx-x)**2+(sy-y)**2
    core=np.exp(-d2/(2*(0.55*r)**2)); halo=np.exp(-d2/(2*(2.6*r)**2))*0.18
    v=(core+halo)*b
    col=np.array([1.0,1.0,1.0])+rng.uniform(-0.08,0.08)*np.array([-1,0,1])
    img[y0:y1,x0:x1]+=v[...,None]*col*255
for _ in range(330): star(rng.uniform(0,W),rng.uniform(0,H*0.97),rng.uniform(0.18,0.55),0.8)
for _ in range(55):  star(rng.uniform(0,W),rng.uniform(0,H*0.97),rng.uniform(0.5,0.9),1.1)
for _ in range(9):   star(rng.uniform(20,W-20),rng.uniform(15,H-60),rng.uniform(0.8,1.0),1.8)
img=np.clip(img,0,255)
# darker, calmer area behind the text so it stays readable
mask=np.zeros((H,W),np.float32); mask[40:442,60:580]=1
mask=cv2.GaussianBlur(mask,(0,0),38)[...,None]
img=img*(1-0.55*mask)+np.array([4,6,12],np.float32)*0.55*mask
# vignette
v=1-0.35*(((xx-W/2)/(W/2))**2+((yy-H/2)/(H/2))**2)[...,None]*0.7
img=np.clip(img*v,0,255).astype(np.uint8)
bg_img=Image.fromarray(img).convert('RGBA'); 

base=bg_img; d=ImageDraw.Draw(base)
def cinzel(s,w=700):
    f=ImageFont.truetype('fonts/Cinzel.ttf',s); f.set_variation_by_axes([w]); return f
INTER='fonts/Inter-%s.otf'
def inter(n,s): return ImageFont.truetype(INTER%n,s)
def tracked_w(t,f,tr): return sum(d.textlength(c,font=f) for c in t)+tr*(len(t)-1)
def tracked(y,t,f,tr,gradient=None,fill=None):
    w=tracked_w(t,f,tr); x=(W-w)/2
    layer=Image.new('L',(W,H),0); ld=ImageDraw.Draw(layer); cx=x
    for c in t:
        ld.text((cx,y),c,font=f,fill=255); cx+=d.textlength(c,font=f)+tr
    sh=Image.new('RGBA',(W,H),(0,0,0,0)); sh.putalpha(layer.point(lambda v:int(v*0.9))); base.alpha_composite(sh,(1,1))
    if gradient:
        top_,bot,y0,y1=gradient; col=np.zeros((H,W,3),np.uint8)
        for r in range(H):
            k=min(1,max(0,(r-y0)/(y1-y0))); col[r,:]=[int(top_[i]*(1-k)+bot[i]*k) for i in range(3)]
        base.paste(Image.fromarray(col),(0,0),layer)
    else: base.paste(Image.new('RGBA',(W,H),fill+(255,)),(0,0),layer)
def centered(y,t,f,col):
    w=d.textlength(t,font=f); x=(W-w)/2
    d.text((x+1,y+1),t,font=f,fill=(0,0,0,255)); d.text((x,y),t,font=f,fill=col)
def wrap(t,f,maxw):
    out=[];cur=''
    for wd in t.split():
        trial=(cur+' '+wd).strip()
        if d.textlength(trial,font=f)<=maxw: cur=trial
        else: out.append(cur);cur=wd
    return out+[cur]
LIGHT=(247,226,186); DARK=(176,128,74); TAN=(200,180,155); white=(242,242,242); grey=(190,190,190)
def rule(y,half=210):
    for x in range(320-half,320+half):
        k=1-abs(x-320)/float(half); d.point((x,y),fill=TAN+(int(255*min(1,k*1.6)),))
y=56
tracked(y,"STAR WARS EPISODE III",cinzel(27,800),3,gradient=(LIGHT,DARK,y+3,y+30)); y+=40
tracked(y,"REVENGE OF THE SITH",cinzel(17,600),4,fill=TAN); y+=34
rule(y); y+=22
tracked(y,"UNOFFICIAL CO-OP MOD",cinzel(33,800),3,gradient=(LIGHT,DARK,y+4,y+36)); y+=52
bf=inter('Medium',17)
lines=wrap("On any Single Player Story Mission, press Back on a controller inserted into Port 2 to activate Co-Op.",bf,470)
lines+=["Drop-in and drop-out whenever*","On boss levels, player 2 takes over the boss."]
for ln in lines: centered(y,ln,bf,white); y+=23
y+=3
centered(y,"*drop-in drop-out disabled for cutscenes and required sections of gameplay",inter('Regular',13),(210,210,210)); y+=26
centered(y,"GitHub.com/CouchCoOpRedux/swrots-coop-mod",inter('SemiBold',16),(226,190,128)); y+=30
sf=inter('Regular',13)
for ln in wrap("This fan-made mod was made with the help of documentation from jedijosh920's SWROTS-PC port, and with Claude AI",sf,470):
    centered(y,ln,sf,grey); y+=17
y+=4
centered(y,"Not affiliated with LucasArts or Lucasfilm Ltd.",sf,grey)
print('last y',y+15)
base.convert('RGB').save('legal_screen.png')
