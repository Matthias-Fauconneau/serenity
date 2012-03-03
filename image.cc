#include "image.h"
#include "stream.h"
#include "vector.h"
#include "string.h"
#include <zlib.h>

template<template <typename> class T, int N> void filter(byte4* dst, const byte* raw, int width, int height) {
    typedef vector<T,uint8,N> S;
    typedef vector<T,int,N> V;
    S prior[width]; clear(prior,width,S(zero));
    for(int y=0;y<height;y++,raw+=width*sizeof(S),dst+=width) {
        int filter = *raw++; assert(filter>=0 && filter<=4);
        S* src = (S*)raw;
        S a=zero;
        if(filter==0) for(int i=0;i<width;i++) dst[i]= prior[i]=      src[i];
        if(filter==1) for(int i=0;i<width;i++) dst[i]= prior[i]= a= a+src[i];
        if(filter==2) for(int i=0;i<width;i++) dst[i]= prior[i]=      prior[i]+src[i];
        if(filter==3) for(int i=0;i<width;i++) dst[i]= prior[i]= a= S((V(prior[i])+V(a))/2)+src[i];
        if(filter==4) {
            V b=zero;
            for(int i=0;i<width;i++) {
                V c = b;
                b = V(prior[i]);
                V d = V(a) + b - c;
                V pa = abs(d-V(a)), pb = abs(d-b), pc = abs(d-c);
                S p; for(int i=0;i<N;i++) p[i]=uint8(pa[i] <= pb[i] && pa[i] <= pc[i] ? a[i] : pb[i] <= pc[i] ? b[i] : c[i]);
                dst[i]= prior[i]=a= p+src[i];
            }
        }
    }
}

Image::Image(array<byte>&& file) {
    NetworkStream s(move(file));
    if(!s.match("\x89PNG\r\n\x1A\n"_)) error("Unknown image format"_);
    z_stream z; clear(z); inflateInit(&z);
    array<byte> idat(s.buffer.size()*16); //FIXME
    z.next_out = (Bytef*)&idat, z.avail_out = (uint)idat.capacity();
    int depth=0;
    while(s) {
        uint32 size = s.read();
        string name = s.read<byte>(4);
        if(name == "IHDR"_) {
            width = (int)(uint32)s.read(), height = (int)(uint32)s.read();
            debug(
                uint8 bitDepth = s.read(), type = s.read(), compression = s.read(), filter = s.read(), interlace = s.read();
                assert(bitDepth==8,(int)bitDepth); assert(compression==0); assert(filter==0); assert(interlace==0);
            )
            release(
                s++; uint8 type = s.read(); s+=3;
            )
            depth = (int[]){0,0,3,0,2,0,4}[type];
            if(!depth) return;
        } else if(name == "IDAT"_) {
            z.avail_in = size;
            z.next_in = (Bytef*)&s.read<byte>((int)size);
            inflate(&z, Z_NO_FLUSH);
        } else s += (int)size;
        s+=4; //CRC
    }
    inflate(&z, Z_FINISH);
    inflateEnd(&z);
    idat.setSize( (int)z.total_out );
    assert(idat.size() == height*(1+width*depth), idat.size(), width, height, depth);
    data = allocate<byte4>(width*height);
    /**/ if(depth==2) filter<ia,2>((byte4*)data,&idat,width,height);
    else if(depth==3) filter<rgb,3>((byte4*)data,&idat,width,height);
    else if(depth==4) filter<rgba,4>((byte4*)data,&idat,width,height);
    else error("depth"_,depth);
}

Image& Image::resize(int w, int h) {
    if(w==width && h==height) return *this;
    const byte4* src = data;
    byte4* buffer = allocate<byte4>(w*h);
    byte4* dst = buffer;
    assert(width/w==height/h && !(width%w) && !(height%h)); //integer uniform downscale
    int scale = width/w;
    for(int y=0;y<h;y++) {
        for(int x=0;x<w;x++) {
            int4 s{0,0,0,0};
            for(int i=0;i<scale;i++){
                for(int j=0;j<scale;j++) {
                    s+= int4(src[i*width+j]);
                }
            }
            *dst = byte4(s/(scale*scale));
            src+=scale, dst++;
        }
        src += (scale-1)*width;
    }
    width=w, height=h;
    if(own) delete[] data; data=buffer; own=true;
    return *this;
}

Image& Image::swap() {
    uint32* p = (uint32*)data;
    for(int i=0;i<width*height;i++) {
        p[i] = swap32(p[i]);
    }
    return *this;
}
