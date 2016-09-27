#include "core/image.h"
#include "data.h"
#include "vector.h"
//include "parallel.h"
#include "math.h"
#include "map.h"

static inline double round(double x) { return __builtin_round(x); }

// -- sRGB --

uint8 sRGB_forward[0x1000];  // 4K (FIXME: interpolation of a smaller table might be faster)
__attribute((constructor(1001))) void generate_sRGB_forward() {
 for(uint index: range(sizeof(sRGB_forward))) {
  double linear = (double) index / (sizeof(sRGB_forward)-1);
  double sRGB = linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear;
  assert(abs(linear-(sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92))< 0x1p-50);
  sRGB_forward[index] = round(0xFF*sRGB);
 }
}

float sRGB_reverse[0x100];
__attribute((constructor(1002))) void generate_sRGB_reverse() {
 for(uint index: range(0x100)) {
  double sRGB = (double) index / 0xFF;
  double linear = sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92;
  assert(abs(sRGB-(linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear))< 0x1p-50);
  sRGB_reverse[index] = linear;
  assert(sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))]==index,
    sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))], index);
 }
}

// -- Decode --

string imageFileFormat(const ref<byte> file) {
 if(startsWith(file,"\xFF\xD8"_)) return "JPEG"_;
 else if(startsWith(file,"\x89PNG\r\n\x1A\n"_)) return "PNG"_;
 else if(startsWith(file,"\x00\x00\x01\x00"_)) return "ICO"_;
 else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return "TIFF"_;
 else if(startsWith(file,"BM"_)) return "BMP"_;
 else return ""_;
}

int2 imageSize(const ref<byte> file) {
 BinaryData s(file, true);
 // PNG
 if(s.match(ref<uint8>{0b10001001,'P','N','G','\r','\n',0x1A,'\n'})) {
  for(;;) {
   s.advance(4); // Length
   if(s.read<byte>(4) == "IHDR"_) {
    uint width = s.read(), height = s.read();
    return int2(width, height);
   }
  }
  error("PNG");
 }
 // JPEG
 enum Marker : uint8 {
  StartOfFrame = 0xC0, DefineHuffmanTable = 0xC4, StartOfImage = 0xD8, EndOfImage = 0xD9,
  StartOfSlice = 0xDA, DefineQuantizationTable = 0xDB, DefineRestartInterval = 0xDD, ApplicationSpecific = 0xE0 };
 if(s.match(ref<uint8>{0xFF, StartOfImage})) {
  for(;;){
   s.skip((uint8)0xFF);
   uint8 marker = s.read();
   if(marker == EndOfImage) break;
   if(marker==StartOfSlice) {
    while(s.available(2) && ((uint8)s.peek() != 0xFF || uint8(s.peek(2)[1])<0xC0)) s.advance(1);
   } else {
    uint16 length = s.read(); // Length
    if(marker>=StartOfFrame && marker<=StartOfFrame+2) {
     uint8 precision = s.read(); assert_(precision==8);
     uint16 height = s.read();
     uint16 width = s.read();
     return int2(width, height);
     //uint8 components = s.read();
     //for(components) { ident:8, h_samp:4, v_samp:4, quant:8 }
    } else s.advance(length-2);
   }
  }
  error("JPG");
 }
 error("Unknown image format", hex(file.size<16?file:s.peek(16)));
}

__attribute((weak)) Image decodePNG(const ref<byte>) { error("PNG support not linked"); }
__attribute((weak)) Image decodeJPEG(const ref<byte>) { log("JPEG support not linked"); return {}; }
__attribute((weak)) Image decodeICO(const ref<byte>) { error("ICO support not linked"); }
__attribute((weak)) Image decodeTIFF(const ref<byte>) { error("TIFF support not linked"); }
__attribute((weak)) Image decodeBMP(const ref<byte>) { error("BMP support not linked"); }
__attribute((weak)) Image decodeTGA(const ref<byte>) { error("TGA support not linked"); }

Image decodeImage(const ref<byte> file) {
 if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
 else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
 else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
 else if(startsWith(file,"\x00\x00\x02\x00"_)||startsWith(file,"\x00\x00\x0A\x00"_)) return decodeTGA(file);
 else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return decodeTIFF(file);
 else if(startsWith(file,"BM"_)) return decodeBMP(file);
 else error("Unknown image format", hex(file.slice(0,min<int>(file.size,4))));
}

// -- Rotate --

void flip(Image& image) {
 for(int y=0,h=image.size.y;y<h/2;y++) for(int x=0,w=image.size.x;x<w;x++)
  swap(image(x,y),image(x,h-1-y));
}
Image flip(Image&& image) {
 flip(image);
 return move(image);
}

void rotate(const Image& target, const Image& source) {
 assert_(target.size.x == source.size.y && target.size.y == source.size.x);
 for(int y: range(source.size.y)) for(int x: range(source.size.x)) target(source.size.y-1-y, x) = source(x,y);
}

Image rotateHalfTurn(Image&& target) {
 for(size_t y: range(target.size.y)) for(size_t x: range(target.size.x/2)) swap(target(x,y), target(target.size.x-1-x, y)); // Reverse rows
 for(size_t y: range(target.size.y/2)) for(size_t x: range(target.size.x)) swap(target(x,y), target(x, target.size.y-1-y)); // Reverse columns
 return move(target);
}

Image negate(Image&& target, const Image& source) {
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x)) {
        byte4 BGRA = source(x, y);
        vec3 linear = vec3(sRGB_reverse[BGRA[0]], sRGB_reverse[BGRA[1]], sRGB_reverse[BGRA[2]]);
        int3 negate = int3(round((float(0xFFF)*(vec3(1)-linear))));
        target(x,y) = byte4(sRGB_forward[negate[0]], sRGB_forward[negate[1]], sRGB_forward[negate[2]], BGRA.a);
    }
    return move(target);
}

// -- Resample (3x8bit) --

static void bilinear(const Image& target, const Image& source) {
 //assert_(!source.alpha, source.size, target.size);
 const uint stride = source.stride;
 for(uint y: range(target.size.y)) {
  for(uint x: range(target.size.x)) {
   const uint fx = x*256*(source.size.x-1)/target.size.x, fy = y*256*(source.size.y-1)/target.size.y; //TODO: incremental
   uint ix = fx/256, iy = fy/256;
   uint u = fx%256, v = fy%256;
   const ref<byte4> span = source.slice(iy*stride+ix);
   byte4 d = 0;
   uint a  = ((uint(span[      0][3]) * (256-u) + uint(span[           1][3])  * u) * (256-v)
     + (uint(span[stride][3]) * (256-u) + uint(span[stride+1][3]) * u) * (       v) ) / (256*256);
   if(a) for(int i=0; i<3; i++) { // Interpolates values as if in linear space (not sRGB)
    d[i] = ((uint(span[      0][3]) * uint(span[      0][i]) * (256-u) + uint(span[           1][3]) * uint(span[           1][i]) * u) * (256-v)
      + (uint(span[stride][3]) * uint(span[stride][i]) * (256-u) + uint(span[stride+1][3]) * uint(span[stride+1][i]) * u) * (       v) )
      / (a*256*256);
   }
   d[3] = a;
   target(x, y) = d;
  }
 }
}

void resize(const Image& target, const Image& source) {
 assert_(source && target && source.size != target.size);
 if(target.size > source.size/2u) bilinear(target, source); // Bilinear resample
 else error(target.size, source.size);
}
