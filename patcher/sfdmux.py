#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Mux an MPEG-1 video elementary stream into a CRI Sofdec .sfd the way the
retail Legal_XBOX.sfd is laid out (2048-byte sectors, video only).
usage: sfdmux.py template.sfd video.m1v out.sfd [--frames N]
The template supplies sector 0 (system header) and the two Sofdec header
sectors; frame count / duration fields in them are patched."""
import re, struct, sys
SEC=2048; PERIOD=3006            # 90 kHz ticks per picture, as in the retail file
RATE_TICKS=2048*90000/13302050.0 # mux_rate 0x1e73 field -> 13.856 ticks / sector

def scr_bytes(i):
    t=int(round(i*RATE_TICKS))
    b=bytearray(5)
    b[0]=0x20|(((t>>30)&7)<<1)|1
    b[1]=(t>>22)&0xff
    b[2]=((((t>>15)&0x7f))<<1)|1
    b[3]=(t>>7)&0xff
    b[4]=((t&0x7f)<<1)|1
    return bytes(b)
def pack_hdr(i): return b'\x00\x00\x01\xba'+scr_bytes(i)+bytes([0x88,0x1e,0x73])
def ts(prefix,t):
    return bytes([(prefix<<4)|(((t>>30)&7)<<1)|1,(t>>22)&0xff,(((t>>15)&0x7f)<<1)|1,(t>>7)&0xff,((t&0x7f)<<1)|1])

def split_units(es):
    """a unit = optional seq/GOP headers + one picture, up to the next unit"""
    starts=[m.start() for m in re.finditer(b'\x00\x00\x01[\x00\xb3\xb8]',es)]
    units=[];cur=None
    for k,s in enumerate(starts):
        code=es[s+3]
        if cur is None: cur=s
        if code==0x00:                      # picture start closes this unit's header run
            end=starts[k+1] if k+1<len(starts) else len(es)
            # headers (b3/b8) of the next unit start at starts[k+1]; trailing b7 stays with last
            units.append((cur,end)); cur=None
    # trailing bytes (sequence end code) append to last unit
    last=units[-1]; units[-1]=(last[0],len(es))
    return units

def T(disp):  # 90 kHz time of a display index; the retail file's clock slips 90 ticks every 30 pictures from picture 11 on
    return disp*PERIOD-90*max(0,(disp-11)//30)

def timing(es,units):
    """PTS/DTS per unit (display idx*PERIOD; DTS for anchors as in the retail file)"""
    info=[]
    gop_start=0; gop_count=0; gops=[]
    # first pass: pictures per GOP
    for (a,b) in units:
        has_gop=es[a:b].find(b'\x00\x00\x01\xb8')>=0 and es[a:b].find(b'\x00\x00\x01\xb8')<es[a:b].find(b'\x00\x00\x01\x00')
        if has_gop:
            gops.append(0)
        gops[-1]+=1
    starts=[0]
    for g in gops[:-1]: starts.append(starts[-1]+g)
    gi=-1; pics=[]
    for (a,b) in units:
        u=es[a:b]; pp=u.find(b'\x00\x00\x01\x00')
        g=u.find(b'\x00\x00\x01\xb8')
        if 0<=g<pp: gi+=1
        tr=(u[pp+4]<<2)|(u[pp+5]>>6); typ=(u[pp+5]>>3)&7
        pics.append((starts[gi]+tr,typ))
    out=[];prev_anchor=-1
    for disp,typ in pics:
        pts=T(disp)
        if typ in (1,2):
            dts=T(prev_anchor+1); prev_anchor=disp
            out.append((pts,dts if dts!=pts else None))
        else: out.append((pts,None))
    return out

def pes(payload,pts,dts,extra_stuff=0):
    if pts is None: t=b'\x0f'
    elif dts is None or dts==pts: t=ts(2,pts)
    else: t=ts(3,pts)+ts(1,dts)
    stuff=12-2-len(t)+extra_stuff
    body=b'\xff'*stuff+b'\x60\x2e'+t+payload
    return b'\x00\x00\x01\xe0'+struct.pack('>H',len(body))+body

def mux(template,es,nframes):
    t=open(template,'rb').read()
    units=split_units(es); tm=timing(es,units)
    assert len(units)==nframes,(len(units),nframes)
    sectors=[bytearray(t[i*SEC:(i+1)*SEC]) for i in range(3)]
    # patch Sofdec header fields (frame count, duration, ASCII tags)
    hdr1=bytearray(sectors[1]); hdr2=bytearray(sectors[2])
    # header bytes live at sector1[18:2048] (2030) then sector2[18:18+576]
    blob=bytearray(bytes(hdr1[18:18+2030])+bytes(hdr2[18:18+576]))
    old_frames=struct.unpack('<I',blob[0xae:0xb2])[0]; old_ms=struct.unpack('<I',blob[0xaa:0xae])[0]
    ms=int(round(nframes*1001/30.0)) ; ms=int(round(nframes*1001/30.0))
    blob[0xaa:0xae]=struct.pack('<I',ms); blob[0xae:0xb2]=struct.pack('<I',nframes)
    def rep(off,old,new):
        assert blob[off:off+len(old)]==old,(hex(off),bytes(blob[off:off+len(old)]))
        assert len(old)==len(new); blob[off:off+len(old)]=new
    o=str(old_frames).encode(); n=str(nframes).encode()
    assert len(o)==len(n)
    for pat,nw in ((b'970\x00'+o+b'\x00',b'970\x00'+n+b'\x00'),(b'DCD\x0010\x00     '+o+b'\x00',b'DCD\x0010\x00     '+n+b'\x00')):
        k=blob.find(pat); assert k>=0,pat; blob[k:k+len(pat)]=nw
    m=re.search(rb' {2,}'+str(old_frames-1).encode()+rb'\x00',bytes(blob)); assert m
    lo=str(old_frames-1).encode(); nl=str(nframes-1).encode(); assert len(lo)==len(nl)
    k=m.end()-1-len(lo); blob[k:k+len(lo)]=nl
    sectors[1][18:18+2030]=blob[:2030]; sectors[2][18:18+576]=blob[2030:2606]
    out=list(sectors); idx=3
    for (a,b),(pts,dts) in zip(units,tm):
        u=es[a:b]; pos=0
        while pos<len(u):
            chunk=u[pos:pos+2018]; first=(pos==0); pos+=len(chunk)
            tp,td=(pts,dts) if first else (None,None)
            p=pes(chunk,tp,td)
            rem=SEC-12-len(p)
            sec=bytearray(pack_hdr(idx))
            if rem==0: sec+=p
            elif rem>=7: sec+=p+b'\x00\x00\x01\xbe'+struct.pack('>H',rem-6)+b'\x0f'+b'\xff'*(rem-7)
            else: sec+=pes(chunk,tp,td,extra_stuff=rem)
            assert len(sec)==SEC,len(sec)
            out.append(sec); idx+=1
    end=bytearray(b'\x00\x00\x01\xb9'+b'\xff'*(SEC-4)); out.append(end)
    return b''.join(bytes(s) for s in out), idx
if __name__=='__main__':
    tpl,vid,outp=sys.argv[1:4]; nf=int(sys.argv[4])
    data,idx=mux(tpl,open(vid,'rb').read(),nf)
    open(outp,'wb').write(data); print('sectors',len(data)//SEC,'bytes',len(data))
