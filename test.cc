#include "thread.h"
#include "png.h"
#include "time.h"
#include "rle.h"

#if 0
struct Test {
 Test() {
  Time time{true};
  string name = arguments()[0];
  String imageFile; int2 size;
  auto list = Folder(".").list(Files);
  for(const String& file: list) {
   TextData s (file);
   if(!s.match(name)) continue;
   if(!s.match(".")) continue;
   if(!s.isInteger()) continue;
   const uint w = s.integer(false);
   if(!s.match("x")) continue;
   if(!s.isInteger()) continue;
   const uint h = s.integer(false);
   if(!s.match(".rle")) continue;
   if(s) continue;
   size = int2(w, h);
   imageFile = copyRef(file);
  }
  if(!imageFile) return;
  buffer<uint8> transpose = decodeRunLength(cast<uint8>(readFile(imageFile)));
  Image8 image = Image8(size);
  log(time, size, float(transpose.size)/1024/1024,"M", float(transpose.size)/readFile(imageFile).size);
  //for(int x: range(image.size.x)) for(int y: range(image.size.y)) image[y*image.stride+x] = transpose[x*image.size.y+y];
  log(time);
 }
} test;
#else
Image8 toImage8(const Image& image) {
 return Image8(apply(image, [](byte4 bgr){
                //assert_(bgr.b==bgr.g && bgr.g==bgr.r, bgr.b, bgr.g, bgr.r);
                return (uint8)((bgr.b+bgr.g+bgr.r)/3); // FIXME: coefficients ?
               }), image.size);
}

struct Test {
 Test() {
  Time time{true};
  string name = arguments()[0];
  String imageFile; int2 size;
  auto list = Folder(".").list(Files);
  for(const String& file: list) {
   TextData s (file);
   if(!s.match(name)) continue;
   if(!s.match(".")) continue;
   if(!s.isInteger()) continue;
   const uint w = s.integer(false);
   if(!s.match("x")) continue;
   if(!s.isInteger()) continue;
   const uint h = s.integer(false);
   if(s) continue;
   size = int2(w, h);
   imageFile = copyRef(file);
  }
  if(!imageFile) {
   Image image = decodePNG(readFile(name+".png", Folder(".")));
   size = image.size;
   imageFile = name+"."+strx(size);
   writeFile(imageFile, cast<byte>(toImage8(image)), Folder("."));
  }
  Map rawImageFileMap = Map(imageFile, Folder("."));
  Image8 image = Image8(cast<uint8>(unsafeRef(rawImageFileMap)), size);
  log(time, image.size, float(image.ref::size)/1024/1024,"M");
#if 0
  Encoder encoder (image.ref::size);
  for(int x: range(image.size.x)) {
   for(int y: range(image.size.y)) {
    encoder.write(image(x, y));
   }
  }
  buffer<byte> encoded = encoder.end();
  log(encoded.size/1024,"K", (float)encoded.size/image.ref::size);
#else
  buffer<uint8> transpose (image.ref::size);
  for(int y: range(image.size.y)) for(int x: range(image.size.x)) transpose[x*image.size.y+y] = image(x, y);
  buffer<uint8> encoded = encodeRunLength(transpose);
#if 0
  buffer<uint8> decoded = decodeRunLength(encoded);
  assert_(decoded.size == transpose.size);
  for(uint i: range(transpose.size)) {
   if(decoded[i] != transpose[i]) {
    log(hex(transpose.slice(0, 40)));
    log(hex(decodeRunLength(encoded).slice(0, 40)));
   }
  }
  assert_(decoded == transpose);
#endif
  log(float(encoded.size)/1024/1024,"M", (float)transpose.size/encoded.size);
#endif
  writeFile(name+"."+strx(size)+".rle", cast<byte>(encoded), Folder("."));
 }
} test;
#endif
