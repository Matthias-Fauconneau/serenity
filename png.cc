#include "png.h"
#include "inflate.h"
#include "data.h"
#include "memory.h"

template<class T> struct rgba { T r,g,b,a; operator byte4() const { return byte4 __(b,g,r,a); } };
template<class T> struct rgb { T r,g,b; operator byte4() const { return byte4 __(b,g,r,255); } };
template<class T> struct ia { T i,a; operator byte4() const {return byte4 __(i,i,i,a); } };
template<class T> struct luma { T i; operator byte4() const {return byte4 __(i,i,i,255); } };

typedef vector<rgb,uint8,3> rgb3;

template<template<typename> class T, int N> void unfilter(byte4* dst, const byte* raw, uint width, uint height, uint xStride, uint yStride) {
    typedef vector<T,uint8,N> S;
    typedef vector<T,int,N> V;
#if __clang__
    byte prior_[width*sizeof(S)]; clear(prior_,sizeof(prior_)); S* prior = (S*)prior_;
#else
    S prior[width]; clear(prior,width,S(0));
#endif
    for(uint y=0;y<height;y++,raw+=width*sizeof(S),dst+=yStride*xStride*width) {
        uint filter = *raw++; assert(filter<=4,"Unknown PNG filter",filter);
        S* src = (S*)raw;
        S a=0;
        if(filter==0) for(uint i=0;i<width;i++) dst[xStride*i]= prior[i]=      src[i];
        if(filter==1) for(uint i=0;i<width;i++) dst[xStride*i]= prior[i]= a= a+src[i];
        if(filter==2) for(uint i=0;i<width;i++) dst[xStride*i]= prior[i]=      prior[i]+src[i];
        if(filter==3) for(uint i=0;i<width;i++) dst[xStride*i]= prior[i]= a= S((V(prior[i])+V(a))/2)+src[i];
        if(filter==4) {
            V b=0;
            for(uint i=0;i<width;i++) {
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
template void unfilter<luma,1>(byte4* dst, const byte* raw, uint width, uint height, uint xStride, uint yStride);
template void unfilter<ia,2>(byte4* dst, const byte* raw, uint width, uint height, uint xStride, uint yStride);
template void unfilter<rgb,3>(byte4* dst, const byte* raw, uint width, uint height, uint xStride, uint yStride);
template void unfilter<rgba,4>(byte4* dst, const byte* raw, uint width, uint height, uint xStride, uint yStride);

Image decodePNG(const ref<byte>& file) {
    BinaryData s(array<byte>(file.data,file.size), true);
    if(s.read<byte>(8)!="\x89PNG\r\n\x1A\n"_) { warn("Invalid PNG"); return Image(); }
    array<byte> buffer;
    uint width=0,height=0,depth=0; uint8 bitDepth=0, type=0, interlace=0;
    array<byte4> palette; bool alpha=false;
    for(;;) {
        uint32 size = s.read();
        uint32 tag = s.read<uint32>();
        if(tag == raw<uint32>("IHDR"_)) {
            width = s.read(), height = s.read();
            bitDepth = s.read(); if(bitDepth!=8 && bitDepth != 4){ warn("Unsupported PNG depth"_,bitDepth,width,height); return Image(); }
            type = s.read(); ____(depth = (int[])__(1,0,3,1,2,0,4)[type];) assert(depth>0&&depth<=4,type);
            alpha = depth==2||depth==4;
            uint8 unused compression = s.read(); assert(compression==0);
            uint8 unused filter = s.read(); assert(filter==0);
            interlace = s.read();
        } else if(tag == raw<uint32>("IDAT"_)) {
            buffer << s.read<byte>(size);
        } else if(tag == raw<uint32>("IEND"_)) {
            assert(size==0);
            s.advance(4); //CRC
            break;
        } else if(tag == raw<uint32>("PLTE"_)) {
            ref<rgb3> plte = s.read<rgb3>(size/3);
            palette.reserve(plte.size); palette.setSize(plte.size);
            for(uint i: range(plte.size)) palette[i]=plte[i];
        }  else if(tag == raw<uint32>("tRNS"_)) {
            ref<byte> trns = s.read<byte>(size);
            assert(trns.size<=palette.size());
            for(uint i: range(trns.size)) palette[i].a=trns[i];
            alpha=true;
        } else {
            s.advance(size);
        }
        s.advance(4); //CRC
        assert(s);
    }
    array<byte> data = inflate(buffer, true);
    if(bitDepth==4) {
        assert(depth==1,depth);
        assert(width%2==0);
        if(data.size() != height*(1+width*depth*bitDepth/8)) { warn("Invalid PNG",data.size(),height*(1+width*depth),width,height,depth); return Image(); }
        const byte* src = data.data();
        array<byte> bytes; bytes.grow(height*(1+width*depth));
        byte* dst = bytes.data();
        for(uint y=0;y<height;y++) {
            dst[0] = src[0]; src++; dst++;
            for(uint x=0;x<width/2;x++) dst[2*x+0]=src[x]>>4, dst[2*x+1]=src[x]&0b1111;
            src+=width/2; dst += width;
        }
        data = move(bytes);
    }
    if(data.size() < height*(1+width*depth)) { warn("Invalid PNG",data.size(),height*(1+width*depth),width,height,depth); return Image(); }
    byte4* image = allocate<byte4>(width*height);
    int w=width,h=height;
    const byte* src=data.data();
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
        if(depth==1) unfilter<luma,1>(image+offset,src,w,h,xStride,yStride);
        if(depth==2) unfilter<ia,2>(image+offset,src,w,h,xStride,yStride);
        if(depth==3) unfilter<rgb,3>(image+offset,src,w,h,xStride,yStride);
        if(depth==4) unfilter<rgba,4>(image+offset,src,w,h,xStride,yStride);
        src += h*(1+w*depth);
    }
    if(type==3) {
        assert(palette);
        for(uint i: range(width*height)) image[i]=palette[image[i][0]];
    }
    return Image(image,width,height,width,true,alpha);
}

uint32 crc32(const ref<byte>& data) {
    static uint crc_table[256];
    static bool unused once = ({for(uint n: range(256)){ uint c=n; for(uint unused k: range(8)) { if(c&1) c=0xedb88320L^(c>>1); else c=c>>1; } crc_table[n] = c; } true;});
    uint crc = 0xFFFFFFFF;
    for(byte b: data) crc = crc_table[(crc ^ b) & 0xff] ^ (crc >> 8);
    return ~crc;
}

uint adler32(const ref<byte> data) {
    const byte* ptr=data.data; uint len=data.size;
    uint a=1, b=0;
    while(len > 0) {
        uint tlen = len > 5552 ? 5552 : len; len -= tlen;
        do { a += *ptr; ptr++; b += a; } while (--tlen);
        a %= 65521, b %= 65521;
    }
    return a | (b << 16);
}

array<byte> filter(const Image& image) {
    uint w=image.width, h=image.height;
    array<byte> data(w*h*4+h); data.setSize(data.capacity());
    byte* dst = data.data(); const byte* src = (byte*)image.data;
    for(uint unused y: range(h)) { *dst++ = 0; for(uint x: range(w)) ((byte4*)dst)[x]=byte4(src[x*4+2],src[x*4+1],src[x*4+0],src[x*4+3]); dst+=w*4, src+=w*4; }
    return data;
}

array<byte> encodePNG(const Image& image) {
    array<byte> file = string("\x89PNG\r\n\x1A\n"_);
    struct { uint32 w,h; uint8 depth, type, compression, filter, interlace; } _packed ihdr = __( .w=big32(image.width), .h=big32(image.height), .depth=8, .type=6, .compression=0, .filter=0, .interlace=0 );
    array<byte> IHDR = "IHDR"_+raw(ihdr);
    file<< raw(big32(IHDR.size()-4)) << IHDR << raw(big32(crc32(IHDR)));

    array<byte> IDAT = "IDAT"_+"\x78\x01"_; //zlib header: method=8, window=7, check=0, level=1
    array<byte> data = filter(image);
    for(uint i=0; i<data.size();) {
        uint16 len = min(data.size()-i,65535u), nlen = ~len;
        IDAT << (i+len==data.size()) << raw(len) << raw(nlen) << data.slice(i,len);
        i+=len;
    }
    IDAT<< raw(big32(adler32(data)));
    file<< raw(big32(IDAT.size()-4)) << IDAT << raw(big32(crc32(IDAT)));

    file<<raw(big32(0))<<"IEND"_<<raw(big32(crc32("IEND"_)));
    return file;
}
