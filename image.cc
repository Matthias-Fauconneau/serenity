#include "image.h"
#include "stream.h"
#include "vector.h"
#include "string.h"
#include <zlib.h>

#include "array.cc"
template class array<byte4>;

Image::Image(array<byte4>&& data, uint width, uint height):data((byte4*)data.data()),width(width),height(height),own(true) {
    assert(data.size() >= width*height, data.size(), width, height);
    data.buffer.capacity = 0; //taking ownership
}

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
    DataBuffer s(move(file)); s.bigEndian=true;
    if(!s.match("\x89PNG\r\n\x1A\n"_)) error("Unknown image format"_);
    z_stream z; clear(z); inflateInit(&z);
    array<byte> idat(s.buffer.size()*16); //FIXME
    z.next_out = (Bytef*)idat.data(), z.avail_out = (uint)idat.capacity();
    int depth=0;
    while(s) {
        uint32 size = s.read();
        string name = s.read<byte>(4);
        if(name == "IHDR"_) {
            width = (int)(uint32)s.read(), height = (int)(uint32)s.read();
            uint8 unused bitDepth = s.read(); assert(bitDepth==8,(int)bitDepth);
            uint8 type = s.read(); depth = (int[]){0,0,3,0,2,0,4}[type]; assert(depth,type);
            uint8 unused compression = s.read(); assert(compression==0);
            uint8 unused filter = s.read(); assert(filter==0);
            uint8 unused interlace = s.read(); assert(interlace==0);
        } else if(name == "IDAT"_) {
            z.avail_in = size;
            auto buffer = s.read<byte>(size);
            z.next_in = (Bytef*)buffer.data();
            inflate(&z, Z_NO_FLUSH);
        } else s.advance(size);
        s.advance(4); //CRC
    }
    inflate(&z, Z_FINISH);
    inflateEnd(&z);
    idat.setSize( (int)z.total_out );
    assert(idat.size() == height*(1+width*depth), idat.size(), width, height, depth, z.avail_in);
    data = allocate<byte4>(width*height);
    /**/ if(depth==2) filter<ia,2>(data,idat.data(),width,height);
    else if(depth==3) filter<rgb,3>(data,idat.data(),width,height);
    else if(depth==4) filter<rgba,4>(data,idat.data(),width,height);
    else error("depth"_,depth);
}

Image resize(const Image& image, uint width, uint height) {
    if(width==image.width && height==image.height) return copy(image);
    assert(image.width/width==image.height/height && !(image.width%width) && !(image.height%height)); //integer uniform downscale
    Image target(width,height);
    const byte4* src = image.data;
    byte4* dst = target.data;
    int scale = image.width/width;
    for(uint y=0; y<height; y++) {
        for(uint x=0; x<width; x++) {
            int4 s=zero;
            for(int i=0;i<scale;i++){
                for(int j=0;j<scale;j++) {
                    s+= int4(src[i*image.width+j]);
                }
            }
            *dst = byte4(s/(scale*scale));
            src+=scale, dst++;
        }
        src += (scale-1)*image.width;
    }
    return target;
}

#define swap32 __builtin_bswap32
Image swap(Image&& image) {
    uint32* p = (uint32*)image.data;
    for(uint i=0;i<image.width*image.height;i++) p[i] = swap32(p[i]);
    return move(image);
}
