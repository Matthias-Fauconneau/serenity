#include "image.h"
#include "stream.h"
#include "inflate.h"
#include "memory.h"


template<class T> struct rgba { T r,g,b,a; operator byte4()const{return byte4{b,g,r,a};} };
template<class T> struct rgb { T r,g,b; operator byte4()const{return byte4{b,g,r,255};} };
template<class T> struct ia { T i,a; operator byte4()const{return byte4{i,i,i,a};}};
template<class T> struct luma { T i; operator byte4()const{return byte4{i,i,i,255};}};

typedef vector<rgb,uint8,3> rgb3;

template<template<typename> class T, int N> void filter(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride) {
    typedef vector<T,uint8,N> S;
    typedef vector<T,int,N> V;
    byte buffer[width*sizeof(S)]; S* prior = (S*)buffer; clear(prior,width,S(0));
    for(int y=0;y<height;y++,raw+=width*sizeof(S),dst+=yStride*xStride*width) {
        uint filter = *raw++; debug( if(filter>4) warn("Unknown PNG filter",filter); )
        S* src = (S*)raw;
        S a(0);
        if(filter==0) for(int i=0;i<width;i++) dst[xStride*i]= prior[i]=      src[i];
        if(filter==1) for(int i=0;i<width;i++) dst[xStride*i]= prior[i]= a= a+src[i];
        if(filter==2) for(int i=0;i<width;i++) dst[xStride*i]= prior[i]=      prior[i]+src[i];
        if(filter==3) for(int i=0;i<width;i++) dst[xStride*i]= prior[i]= a= S((V(prior[i])+V(a))/2)+src[i];
        if(filter==4) {
            V b(0);
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
template void filter<luma,1>(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride);
template void filter<ia,2>(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride);
template void filter<rgb,3>(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride);
template void filter<rgba,4>(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride);

Image<byte4> decodePNG(const ref<byte>& file) {
    DataStream s(array<byte>(file.data,file.size), true);
    assert(s.get(8)=="\x89PNG\r\n\x1A\n"_);
    s.advance(8);
    array<byte> buffer;
    uint width=0,height=0,depth=0; uint8 type=0, interlace=0;
    array<byte> palette;
    for(;;) {
        uint32 size = s.read();
        array<byte> name = s.read(4);
        if(name == "IHDR"_) {
            width = s.read(), height = s.read();
            uint8 unused bitDepth = s.read();
            if(bitDepth!=8){ warn("Unsupported PNG bitdepth"_,bitDepth); return Image<byte4>(); }
            type = s.read(); depth = (int[]){1,0,3,1,2,0,4}[type]; assert(depth>0&&depth<=4,type);
            uint8 unused compression = s.read(); assert(compression==0);
            uint8 unused filter = s.read(); assert(filter==0);
            interlace  = s.read();
        } else if(name == "IDAT"_) {
            buffer << s.read(size);
        } else if(name=="IEND"_) {
            assert(size==0);
            s.advance(4); //CRC
            break;
        } else if(name == "PLTE"_) {
            palette = s.read(size);
        } else {
            s.advance(size);
        }
        s.advance(4); //CRC
        assert(s);
    }
    array<byte> data = inflate(buffer, true);
    if(data.size() < height*(1+width*depth)) { warn("Invalid PNG"); return Image<byte4>(); }
    byte4* image = allocate<byte4>(width*height);
    int w=width,h=height;
    byte* src=data.data();
    for(int i=0;i==0 || (interlace && i<7);i++) {
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
        if(depth==1) filter<luma,1>(image+offset,src,w,h,xStride,yStride);
        if(depth==2) filter<ia,2>(image+offset,src,w,h,xStride,yStride);
        if(depth==3) filter<rgb,3>(image+offset,src,w,h,xStride,yStride);
        if(depth==4) filter<rgba,4>(image+offset,src,w,h,xStride,yStride);
        src += h*(1+w*depth);
    }
    if(type==3) { assert(palette);
        rgb3* lookup = (rgb3*)palette.data();
        for(uint i=0;i<width*height;i++) image[i]=lookup[image[i].r];
    }
    return Image<byte4>(image,width,height,width,true,depth==4);
}
