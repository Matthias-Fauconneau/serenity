#include "image.h"
#include "stream.h"
#include <zlib.h>

template<template <typename> class T, int N> void filter(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride) {
    typedef vector<T,uint8,N> S;
    typedef vector<T,int,N> V;
    S prior[width]; clear(prior,width,S(zero));
    for(int y=0;y<height;y++,raw+=width*sizeof(S),dst+=yStride*xStride*width) {
        int filter = *raw++; assert(filter>=0 && filter<=4);
        S* src = (S*)raw;
        S a=zero;
        if(filter==0) for(int i=0;i<width;i++) dst[xStride*i]= prior[i]=      src[i];
        if(filter==1) for(int i=0;i<width;i++) dst[xStride*i]= prior[i]= a= a+src[i];
        if(filter==2) for(int i=0;i<width;i++) dst[xStride*i]= prior[i]=      prior[i]+src[i];
        if(filter==3) for(int i=0;i<width;i++) dst[xStride*i]= prior[i]= a= S((V(prior[i])+V(a))/2)+src[i];
        if(filter==4) {
            V b=zero;
            for(int i=0;i<width;i++) {
                V c = b;
                b = V(prior[i]);
                V d = V(a) + b - c;
                V pa = abs(d-V(a)), pb = abs(d-b), pc = abs(d-c);
                S p; for(int i=0;i<N;i++) p[i]=uint8(pa[i] <= pb[i] && pa[i] <= pc[i] ? a[i] : pb[i] <= pc[i] ? b[i] : c[i]);
                dst[xStride*i]= prior[i]=a= p+src[i];
            }
        }
    }
}

Image decodePNG(const array<byte>& file) {
    DataBuffer s(array<byte>(file.data(),file.size())); s.bigEndian=true;
    if(s.get(8)!="\x89PNG\r\n\x1A\n"_) error("Invalid PNG"_);
    s.advance(8);
    z_stream z; clear(z); inflateInit(&z);
    array<byte> idat(s.buffer.size()*16); //FIXME
    z.next_out = (Bytef*)idat.data(), z.avail_out = (uint)idat.capacity();
    uint width=0,height=0,depth=0; uint8 type=0, interlace=0;
    array<byte> palette;
    for(;;) {
        uint32 size = s.read();
        string name = s.read(4);
        if(name == "IHDR"_) {
            width = s.read(), height = s.read();
            uint8 unused bitDepth = s.read(); assert(bitDepth==8,(int)bitDepth);
            type = s.read(); depth = (int[]){1,0,3,1,2,0,4}[type]; assert(depth>0&&depth<=4,type);
            uint8 unused compression = s.read(); assert(compression==0);
            uint8 unused filter = s.read(); assert(filter==0);
            interlace  = s.read();
        } else if(name == "IDAT"_) {
            z.avail_in = size;
            auto buffer = s.read(size);
            z.next_in = (Bytef*)buffer.data();
            inflate(&z, Z_NO_FLUSH);
        } else if(name=="IEND"_) {
            assert(size==0);
            s.advance(4); //CRC
            break;
        } else if(name == "PLTE"_) {
            palette = s.read(size);
        } else { if(name!="sBIT"_&&name!="pHYs"_&&name!="tEXt"_) log(name); s.advance(size); }
         s.advance(4); //CRC
         assert(s);
    }
    inflate(&z, Z_FINISH);
    inflateEnd(&z);
    idat.setSize( (int)z.total_out );
    if(idat.size() < height*(1+width*depth)) { warn("Invalid PNG"); return Image(); }
    byte* data= idat.data();
    byte4* image = allocate<byte4>(width*height);
    int w=width,h=height;
    for(int i=0;interlace && i<7;i++) {
        int xStride=1,yStride=1;
        int offset=0;
        if(interlace) {
            if(i==0) xStride=8, yStride=8;
            if(i==1) xStride=8, yStride=8, offset=4;
            if(i==2) xStride=4, yStride=8, offset=4*width;
            if(i==3) xStride=4, yStride=4, offset=2;
            if(i==4) xStride=2, yStride=4, offset=2*width;
            if(i==5) xStride=2, yStride=2, offset=1;
            if(i==6) xStride=1, yStride=2, offset=1*width;
            w=width/xStride;
            h=height/yStride;
        }
        if(depth==1) filter<luma,1>(image+offset,data,w,h,xStride,yStride);
        if(depth==2) filter<ia,2>(image+offset,data,w,h,xStride,yStride);
        if(depth==3) filter<rgb,3>(image+offset,data,w,h,xStride,yStride);
        if(depth==4) filter<rgba,4>(image+offset,data,w,h,xStride,yStride);
        data += h*(1+w*depth);
    }
    if(type==3) { assert(palette);
        rgb3* lookup = (rgb3*)palette.data();
        for(uint i=0;i<width*height;i++) image[i]=lookup[image[i].r];
    }
    return Image(image,width,height,true);
}
