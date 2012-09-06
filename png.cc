#include "png.h"
#include "inflate.h"
#include "stream.h"
#include "memory.h"

template<class T> struct rgba { T r,g,b,a; operator byte4() const { return byte4 __(b,g,r,a); } };
template<class T> struct rgb { T r,g,b; operator byte4() const { return byte4 __(b,g,r,255); } };
template<class T> struct ia { T i,a; operator byte4() const {return byte4 __(i,i,i,a); } };
template<class T> struct luma { T i; operator byte4() const {return byte4 __(i,i,i,255); } };

typedef vector<rgb,uint8,3> rgb3;

template<template<typename> class T, int N> void unfilter(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride) {
    typedef vector<T,uint8,N> S;
    typedef vector<T,int,N> V;
    byte buffer[width*sizeof(S)]; S* prior = (S*)buffer; clear(prior,width,S(0));
    for(int y=0;y<height;y++,raw+=width*sizeof(S),dst+=yStride*xStride*width) {
        uint filter = *raw++; assert(filter<=4,"Unknown PNG filter",filter);
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
template void unfilter<luma,1>(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride);
template void unfilter<ia,2>(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride);
template void unfilter<rgb,3>(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride);
template void unfilter<rgba,4>(byte4* dst, const byte* raw, int width, int height, int xStride, int yStride);

Image decodePNG(const ref<byte>& file) {
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
            if(bitDepth!=8){ warn("Unsupported PNG bitdepth"_,bitDepth,width,height); return Image(); }
            type = s.read(); depth = (int[])__(1,0,3,1,2,0,4)[type]; assert(depth>0&&depth<=4,type);
            uint8 unused compression = s.read(); assert(compression==0);
            uint8 unused filter = s.read(); assert(filter==0);
            interlace = s.read();
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
    const array<byte> data = inflate(buffer, true);
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
        rgb3* lookup = (rgb3*)palette.data();
        for(uint i=0;i<width*height;i++) image[i]=lookup[image[i].r];
    }
    return Image(image,width,height,width,true,depth==2||depth==4);
}

template<uint C, int K = 0> struct crc32__ { static constexpr uint value =  crc32__<(C & 1) ? (0xedb88320L ^ (C >> 1)) : (C >> 1), K+1>::value; };
template<uint C> struct crc32__<C, 8> { static constexpr uint value = C; };
template<int N = 0, uint ...D> struct crc32_ : crc32_<N+1, D..., crc32__<N>::value>  { };
template<uint ...D> struct crc32_<256, D...> { static constexpr uint table[sizeof...(D)] = {  D... }; };
template<uint ...D> constexpr uint crc32_<256, D...>::table[sizeof...(D)];

uint32 crc32(const ref<byte>& data) {
    uint crc = 0xFFFFFFFF;
    for(byte b: data) crc = crc32_<>::table[(crc ^ b) & 0xff] ^ (crc >> 8);
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
    array<byte> data(image.width*image.height*4+image.height); data.setSize(data.capacity());
    byte* dst = data.data(); const byte* src = (byte*)image.data;
    for(uint y=0,w=image.width; y<image.height; y++ ) { *dst++ = 0; copy(dst,src,w*4); dst+=w*4, src+=w*4; }
    return data;
}

array<byte> encodePNG(const Image& image) {
    array<byte> file = string("\x89PNG\r\n\x1A\n"_);
    struct { uint32 w,h; uint8 depth, type, compression, filter, interlace; } packed ihdr = __( .w=big32(image.width), .h=big32(image.height), .depth=8, .type=6 );
    array<byte> IHDR = "IHDR"_+raw(ihdr);
    file<< raw(big32(IHDR.size()-4)) << IHDR << raw(big32(crc32(IHDR)));

    array<byte> IDAT = "IDAT"_+"\x78\x01"_; //zlib header: method=8, window=7, check=0, level=1
    array<byte> data = filter(image);
    for(uint i=0;i<data.size();) {
        uint16 len = min(data.size()-i,65535u), nlen = ~len;
        IDAT << (i+len==data.size()) << raw(len) << raw(nlen) << data.slice(i,len);
        i+=len;
    }
    IDAT<< raw(big32(adler32(data)));
    file<< raw(big32(IDAT.size()-4)) << IDAT << raw(big32(crc32(IDAT)));

    file<<raw(big32(0))<<"IEND"_<<raw(big32(crc32("IEND"_)));
    return file;
}
