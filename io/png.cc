#include "png.h"
#include "data.h"
#include "deflate.h"

generic struct rgba { T r,g,b,a; operator byte4() const { return byte4 {b,g,r,a}; } };
generic struct rgb { T r,g,b; operator byte4() const { return byte4 {b,g,r,255}; } };
generic struct ia { T i,a; operator byte4() const {return byte4 {i,i,i,a}; } };
generic struct luma { T i; operator byte4() const {return byte4 {i,i,i,255}; } };

typedef vec<rgb,uint8,3> rgb3;

template<template<typename> Type T, int N> void unfilter(byte4* dst, const byte* raw, uint width, uint height, uint xStride, uint yStride) {
    typedef vec<T,uint8,N> S;
    typedef vec<T,int,N> V;
    buffer<S> prior(width); prior.clear(0);
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

Image decodePNG(const ref<byte> file) {
    BinaryData s(file, true);
    if(s.read<byte>(8)!="\x89PNG\r\n\x1A\n") { error("Invalid PNG"); return Image(); }
    uint width=0,height=0,depth=0; uint8 bitDepth=0, type=0, interlace=0;
    uint palette[256]; bool alpha=false;
    array<byte> buffer;
    for(;;) {
        uint32 size = s.read();
        string tag = s.read<byte>(4);
        if(tag == "IHDR") {
            width = s.read(), height = s.read();
            bitDepth = s.read(); assert(bitDepth==8 || bitDepth == 4 || bitDepth == 1);
            type = s.read(); depth = (int[]){1,0,3,1,2,0,4}[type]; assert(depth>0&&depth<=4,type);
            alpha = depth==2||depth==4;
            uint8 unused compression = s.read(); assert(compression==0);
            uint8 unused filter = s.read(); assert(filter==0);
            interlace = s.read();
        } else if(tag == "IDAT") {
            /*if(!buffer) buffer.data=s.read<byte>(size).data, buffer.size=size; // References first chunk to avoid copy
            else*/ buffer << s.read<byte>(size); // Explicitly concatenates chunks (FIXME: stream inflate)
        } else if(tag == "IEND") {
            assert(size==0);
            s.advance(4); //CRC
            break;
        } else if(tag == "PLTE") {
            ref<rgb3> plte = s.read<rgb3>(size/3);
            assert(plte.size<=256);
            for(uint i: range(plte.size)) (byte4&)palette[i]=plte[i];
        }  else if(tag == "tRNS") {
            ref<byte> trns = s.read<byte>(size);
            assert(trns.size<=256);
            for(uint i: range(trns.size)) ((byte4&)palette[i]).a=trns[i];
            alpha=true;
        } else {
            s.advance(size);
        }
        s.advance(4); //CRC
        assert(s);
    }
    ::buffer<byte> data = inflate(buffer, true);
    if(bitDepth==1 || bitDepth==4) {
        assert(type==0 || type==3, type);
        assert(depth==1,depth);
        assert(width%(8/bitDepth)==0);
        assert(data.size == height*(1+width*depth*bitDepth/8));
        const byte* src = data.data;
        ::buffer<byte> bytes(height*(1+width*depth));
        byte* dst = bytes.begin();
        for(uint y=0;y<height;y++) {
            dst[0] = src[0]; src++; dst++;
            if(bitDepth==1) for(uint x=0;x<width/8;x++) for(uint b: range(8)) dst[8*x+b] = (src[x]&(1<<(7-b))) ? 1 : 0;
            if(bitDepth==4) for(uint x=0;x<width/2;x++) dst[2*x+0]=src[x]>>4, dst[2*x+1]=src[x]&0b1111;
            src+=width/(8/bitDepth); dst += width;
        }
        data = move(bytes);
    }
    if(data.size < height*(1+width*depth)) { error("Invalid PNG", data.size, height*(1+width*depth), width, height, depth, bitDepth); return Image(); }
    Image image(width,height,alpha);
    byte4* dst = image.pixels;
    int w=width,h=height;
    const byte* src=data.data;
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
        if(depth==1) unfilter<luma,1>(dst+offset,src,w,h,xStride,yStride);
        if(depth==2) unfilter<ia,2>(dst+offset,src,w,h,xStride,yStride);
        if(depth==3) unfilter<rgb,3>(dst+offset,src,w,h,xStride,yStride);
        if(depth==4) unfilter<rgba,4>(dst+offset,src,w,h,xStride,yStride);
        src += h*(1+w*depth);
    }
    if(type==3) {
        for(uint i: range(width*height)) dst[i]=palette[dst[i][0]];
    }
    return image;
}

uint32 crc32(const ref<byte> data) {
    static uint crc_table[256];
    static int unused once = ({ for(uint n: range(256)) {
                                     uint c=n; for(uint unused k: range(8)) { if(c&1) c=0xedb88320L^(c>>1); else c=c>>1; } crc_table[n] = c; } 0;});
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

buffer<byte> filter(const Image& image) {
    uint w=image.width, h=image.height;
    buffer<byte> data(w*h*4+h);
    byte* dst = data.begin(); const byte* src = (byte*)image.pixels.data;
    for(uint unused y: range(h)) {
        *dst++ = 0;
        for(uint x: range(w)) ((byte4*)dst)[x]=byte4(src[x*4+2],src[x*4+1],src[x*4+0],image.alpha?src[x*4+3]:0xFF);
        dst+=w*4, src+=image.stride*4;
    }
    return data;
}

buffer<byte> encodePNG(const Image& image) {
    array<byte> file = String("\x89PNG\r\n\x1A\n");
    struct { uint32 w,h; uint8 depth, type, compression, filter, interlace; } packed ihdr { big32(image.width), big32(image.height), 8, 6, 0, 0, 0 };
    array<byte> IHDR = "IHDR"+raw(ihdr);
    file<< raw(big32(IHDR.size-4)) << IHDR << raw(big32(crc32(IHDR)));

    array<byte> IDAT = "IDAT"+deflate(filter(image),true);
    file<< raw(big32(IDAT.size-4)) << IDAT << raw(big32(crc32(IDAT)));

    file<<raw(big32(0))<<"IEND"<<raw(big32(crc32("IEND")));
    return move(file);
}
