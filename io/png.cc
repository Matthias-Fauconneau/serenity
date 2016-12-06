#include "png.h"
#include "data.h"
#include "deflate.h"

generic struct ia { T i,a; operator byte4() const {return byte4(i,i,i,a); } };
generic struct luma { T i; operator byte4() const {return byte4(i,i,i,0xFF); } };

typedef vec<rgb,uint8,3> rgb3;

// Paeth median
template<template<Type> class T, uint N> vec<T, uint8, N> Paeth(vec<T, int, N> a, vec<T, int, N> b, vec<T, int, N> c) {
 vec<T, int, N> d = a + b - c;
 vec<T, int, N> pa = abs(d-a), pb = abs(d-b), pc = abs(d-c);
 vec<T, uint8, N> p; for(uint i=0;i<N;i++) p[i]=uint8(pa[i] <= pb[i] && pa[i] <= pc[i] ? a[i] : pb[i] <= pc[i] ? b[i] : c[i]);
 return p;
}

enum class Predictor { None, Left, Up, Average, Paeth };

template<template<Type> class T, int N>
void unpredict(byte4* target, const byte* source, size_t width, int height, size_t xStride, size_t yStride) {
 typedef vec<T, uint8, N> U;
 typedef vec<T, int, N> V;
 buffer<U> prior(width); prior.clear(0);
 for(int unused y: range(height)) {
  Predictor predictor = Predictor(*source++);
  const U* src = reinterpret_cast<const U*>(source);
  U a = 0;
  /**/  if(predictor==Predictor::None) for(size_t x: range(width)) target[x*xStride]= prior[x]=     src[x];
  else if(predictor==Predictor::Left) for(size_t x: range(width)) target[x*xStride]= prior[x]= a= a+src[x];
  else if(predictor==Predictor::Up) for(size_t x: range(width)) target[x*xStride]= prior[x]=       prior[x]+src[x];
  else if(predictor==Predictor::Average) for(size_t x: range(width)) target[x*xStride]= prior[x]= a= U((V(prior[x])+V(a))/2)+src[x];
  else if(predictor==Predictor::Paeth) {
   V b=0;
   for(size_t x: range(width)) {
    V c = b;
    b = V(prior[x]);
    target[x*xStride]= prior[x]= a= Paeth<T,N>(V(a), b, c) + src[x];
   }
  }
  else error(int(predictor));
  source += width*sizeof(U);
  target += yStride*xStride*width;
 }
}

Image decodePNG(const ref<byte> file) {
 BinaryData s(file, true);
 if(string(s.read<byte>(8))!="\x89PNG\r\n\x1A\n") { error("Invalid PNG"); return Image(); }
 uint width=0,height=0,depth=0; uint8 bitDepth=0, type=0, interlace=0;
 byte4 palette[256]; bool alpha=false;
 array<byte> IDAT;
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
            else*/ IDAT.append(s.read<byte>(size)); // Explicitly concatenates chunks (FIXME: stream inflate)
  } else if(tag == "IEND") {
   assert(size==0);
   s.advance(4); //CRC
   break;
  } else if(tag == "PLTE") {
   ref<rgb3> plte = s.read<rgb3>(size/3);
   assert(plte.size<=256);
   for(size_t i: range(plte.size)) palette[i] = byte4(plte[i].b, plte[i].g, plte[i].r, 0xFF);
  }  else if(tag == "tRNS") {
   ref<byte> trns = s.read<byte>(size);
   assert(trns.size<=256);
   for(size_t i: range(trns.size)) palette[i].a=trns[i];
   alpha=true;
  } else {
   s.advance(size);
  }
  s.advance(4); //CRC
  assert(s);
 }
 buffer<byte> predicted = inflate(IDAT, true);
 if(bitDepth==1 || bitDepth==4) {
  assert(type==0 || type==3, type);
  assert(depth==1,depth);
  assert(width%(8/bitDepth)==0);
  assert(predicted.size == height*(1+width*depth*bitDepth/8));
  const byte* source = predicted.data;
  ::buffer<byte> unpackedBytes(height*(1+width*depth));
  byte* target = unpackedBytes.begin();
  for(size_t unused y: range(height)) {
   target[0] = source[0]; source++; target++;
   if(bitDepth==1) for(size_t x: range(width/8)) for(size_t b: range(8)) target[8*x+b] = (source[x]&(1<<(7-b))) ? 1 : 0;
   if(bitDepth==4) for(size_t x: range(width/2)) target[2*x+0]=source[x]>>4, target[2*x+1]=source[x]&0b1111;
   source+=width/(8/bitDepth); target += width;
  }
  predicted = move(unpackedBytes);
 }
 assert_(predicted.size == height*(1+width*depth), "Invalid PNG", predicted.size, height*(1+width*depth), width, height, depth, bitDepth);
 Image image(width,height,alpha);
 byte4* target = image.begin();
 int w=width,h=height;
 const byte* source=predicted.data;
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
  if(depth==1) unpredict<luma,1>(target+offset,source,w,h,xStride,yStride);
  if(depth==2) unpredict<ia,2>(target+offset,source,w,h,xStride,yStride);
  if(depth==3) unpredict<rgb,3>(target+offset,source,w,h,xStride,yStride);
  if(depth==4) unpredict<rgba,4>(target+offset,source,w,h,xStride,yStride);
  source += h*(1+w*depth);
 }
 if(type==3) {
  for(size_t i: range(width*height)) target[i]=palette[target[i][0]];
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

template<template<Type> class T, int N> buffer<byte> predict(const byte4* source, size_t width, size_t height) {
 typedef vec<T,uint8,N> U;
 typedef vec<T,int,N> V;
 buffer<U> prior(width); prior.clear(0);
 buffer<byte> data ( height * ( 1 + width*sizeof(U) ));
 byte* target = data.begin();
 for(size_t unused y: range(height)) {
  *target++ = byte(Predictor::Paeth);
  U* dst = (U*)target;
  U a = 0;
  V b=0;
  for(size_t x: range(width)) {
   V c = b;
   b = V(prior[x]);
   dst[x]= U(source[x]) - Paeth<T,N>(V(a), b, c);
   prior[x]=a= source[x];
  }
  source += width;
  target += width*sizeof(U);
 }
 return data;
}

buffer<byte> encodePNG(const Image& image) {
 struct { uint32 w,h; uint8 depth, type, compression, filter, interlace; } packed ihdr
 { big32(image.size.x), big32(image.size.y), 8, uint8(image.alpha?6:2), 0, 0, 0 };
 buffer<byte> IHDR = "IHDR"_+raw(ihdr);

 buffer<byte> predicted;
 if(!image.alpha) predicted = predict<rgb,3>(image.data, image.size.x, image.size.y);
 else predicted = predict<rgba,4>(image.data, image.size.x, image.size.y);
 buffer<byte> IDAT = ref<byte>("IDAT"_)+deflate(predicted, true);

 return "\x89PNG\r\n\x1A\n"_
   +raw(big32(IHDR.size-4)) + IHDR + raw(big32(crc32(IHDR)))
   +raw(big32(IDAT.size-4)) + IDAT + raw(big32(crc32(IDAT)))
   +raw(big32(            4-4)) + "IEND"_ + raw(big32(crc32("IEND"_)));
}
