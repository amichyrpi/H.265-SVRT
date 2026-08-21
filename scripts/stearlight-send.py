#!/usr/bin/env python3
"""Send an Annex-B HEVC stream with AUD NALs over Stearlight UDP/FEC."""
import argparse, math, random, socket, struct, time

MAGIC=0x53544152; SHARD=1164; DATA=10; PARITY=2
def mul(a,b):
    out=0
    while b:
        if b&1: out^=a
        high=a&0x80;a=(a<<1)&0xff
        if high:a^=0x1d
        b>>=1
    return out
def access_units(data):
    starts=[];i=0
    while i+5<len(data):
        n=-1
        if data[i:i+3]==b'\0\0\1':n=i+3
        elif data[i:i+4]==b'\0\0\0\1':n=i+4
        if n>=0 and ((data[n]>>1)&0x3f)==35:starts.append(i)
        i+=1
    if not starts: raise SystemExit('HEVC stream has no AUD NALs; add hevc_metadata=aud=insert')
    boundaries=[0]+starts[1:]+[len(data)]
    return [data[a:b] for a,b in zip(boundaries,boundaries[1:]) if b>a]
def main():
    p=argparse.ArgumentParser();p.add_argument('stream');p.add_argument('host');p.add_argument('port',type=int);p.add_argument('--fps',type=float,default=60);a=p.parse_args()
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);session=random.randrange(1,2**32);frame_time=1/a.fps
    for frame_id,frame in enumerate(access_units(open(a.stream,'rb').read()),1):
        started=time.monotonic();timestamp=time.monotonic_ns()//1000;count=math.ceil(len(frame)/SHARD)
        for group,first in enumerate(range(0,count,DATA)):
            n=min(DATA,count-first);blocks=[]
            for index in range(n):
                offset=(first+index)*SHARD;payload=frame[offset:offset+SHARD];blocks.append(payload.ljust(SHARD,b'\0'))
                header=struct.pack('!IBBHIIQIHBBBBH',MAGIC,2,1,0,session,frame_id,timestamp,len(frame),group,index,n,PARITY,0,len(payload));sock.sendto(header+payload,(a.host,a.port))
            p0=bytearray(SHARD);p1=bytearray(SHARD)
            for index,block in enumerate(blocks):
                for byte,value in enumerate(block):p0[byte]^=value;p1[byte]^=mul(index+1,value)
            for parity,payload in enumerate((p0,p1)):
                header=struct.pack('!IBBHIIQIHBBBBH',MAGIC,2,1,2,session,frame_id,timestamp,len(frame),group,n+parity,n,PARITY,0,SHARD);sock.sendto(header+payload,(a.host,a.port))
        delay=frame_time-(time.monotonic()-started)
        if delay>0:time.sleep(delay)
if __name__=='__main__':main()
