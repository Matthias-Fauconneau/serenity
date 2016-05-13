#include "core/image.h"
#include "data.h"
#include "vector.h"
#include "map.h"
#include "algorithm.h"

// -- sRGB --

inline double pow(double x, double y) { return __builtin_pow(x,y); } // math.h

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
__attribute((constructor(1001))) void generate_sRGB_reverse() {
 for(uint index: range(0x100)) {
  double sRGB = (double) index / 0xFF;
  double linear = sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92;
  assert(abs(sRGB-(linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear))< 0x1p-50);
  sRGB_reverse[index] = linear;
  assert(sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))]==index);
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
__attribute((weak)) Image decodeJPEG(const ref<byte>) { error("JPEG support not linked"); }
__attribute((weak)) Image decodeICO(const ref<byte>) { error("ICO support not linked"); }
__attribute((weak)) Image decodeTIFF(const ref<byte>) { error("TIFF support not linked"); }
__attribute((weak)) Image decodeBMP(const ref<byte>) { error("BMP support not linked"); }
__attribute((weak)) Image decodeTGA(const ref<byte>) { error("TGA support not linked"); }

Image decodeImage(const ref<byte> file) {
 if(!file) return Image();
 else if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
 else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
 else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
 else if(startsWith(file,"\x00\x00\x02\x00"_)||startsWith(file,"\x00\x00\x0A\x00"_)) return decodeTGA(file);
 else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return decodeTIFF(file);
 else if(startsWith(file,"BM"_)) return decodeBMP(file);
 else error("Unknown image format", hex(file.slice(0,min<int>(file.size,4))));
}

// -- Rotate --

Image flip(Image&& image) {
 for(int y=0,h=image.height;y<h/2;y++) for(int x=0,w=image.width;x<w;x++)
  swap(image(x,y),image(x,h-1-y));
 return move(image);
}

void rotate(const Image& target, const Image& source) {
 assert_(target.size.x == source.size.y && target.size.y == source.size.x, source.size, target.size);
 for(int y: range(source.height)) for(int x: range(source.width)) target(source.height-1-y, x) = source(x,y);
}

Image rotateHalfTurn(Image&& target) {
 for(size_t y: range(target.height)) for(size_t x: range(target.width/2)) swap(target(x,y), target(target.width-1-x, y)); // Reverse rows
 for(size_t y: range(target.height/2)) for(size_t x: range(target.width)) swap(target(x,y), target(x, target.height-1-y)); // Reverse columns
 return move(target);
}

// -- Resample (3x8bit) --

static void box(const Image& target, const Image& source) {
 //assert_(!source.alpha); //FIXME: not alpha correct
 //assert_(source.size.x/target.size.x == source.size.y/target.size.y, target, source, source.size.x/target.size.x, source.size.y/target.size.y);
 int scale = min(source.size.x/target.size.x, source.size.y/target.size.y);
 assert_(scale <= 512, target.size, source.size);
 assert_((target.size-int2(1))*scale+int2(scale-1) < source.size, target, source);
 for(size_t y : range(target.height)) {
  const byte4* sourceLine = source.data + y * scale * source.stride;
  byte4* targetLine = target.begin() + y * target.stride;
  for(uint unused x: range(target.width)) {
   const byte4* sourceSpanOrigin = sourceLine + x * scale;
   uint4 sum = 0;
   for(uint i: range(scale)) {
    const byte4* sourceSpan = sourceSpanOrigin + i * source.stride;
    for(uint j: range(scale)) {
     uint4 s (sourceSpan[j]);
     s.b = s.b*s.a; s.g = s.g*s.a; s.r = s.r*s.a;
     sum += uint4(s);
    }
   }
   if(sum.a) { sum.b = sum.b / sum.a; sum.g = sum.g / sum.a; sum.r = sum.r / sum.a; }
   sum.a /= scale*scale;
   targetLine[x] = byte4(sum[0], sum[1], sum[2], sum[3]);
  }
 }
}
static Image box(Image&& target, const Image& source) { box(target, source); return move(target); }

static void bilinear(const Image& target, const Image& source) {
 //assert_(!source.alpha, source.size, target.size);
 const uint stride = source.stride;
 for(size_t y: range(target.height)) {
  for(uint x: range(target.width)) {
   const uint fx = x*256*(source.width-1)/target.width, fy = y*256*(source.height-1)/target.height; //TODO: incremental
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
 assert_(source && target && source.size != target.size, source, target);
 if(source.width%target.width==0 && source.height%target.height==0) box(target, source); // Integer box downsample
 else if(target.size > source.size/2) bilinear(target, source); // Bilinear resample
 else { // Integer box downsample + Bilinear resample
  int downsampleFactor = min(source.size.x/target.size.x, source.size.y/target.size.y);
  assert_(downsampleFactor, target, source);
  bilinear(target, box(Image((source.size)/downsampleFactor, source.alpha), source));
 }
}


void toFloat(mref<float> target, ref<uint8> source) { target.apply([](uint8 v) { return v; }, source); }
ImageF toFloat(ImageF&& target, const Image8& source) { toFloat(target, source); return move(target); }
ImageF toFloat(const Image8& source) { return toFloat(source.size, source); }

void downsample(const Image8& target, const Image8& source) {
 assert_(target.size == source.size/2, target.size, source.size);
 for(uint y: range(target.height)) for(uint x: range(target.width))
  target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / 4;
}
Image8 downsample(const Image8& source) { Image8 target(source.size/2); downsample(target, source); return target; }

void mean (const ImageF& target, const ImageF& buffer, const ImageF& source, uint R) {
 assert(target.size == buffer.size.yx() && buffer.size.yx() == source.size);
 for(size_t i: range(2)) {
  const ImageF& X = *(const ImageF*[]){&source, &buffer}[i];
  const ImageF& Y = *(const ImageF*[]){&buffer, &target}[i];
  for(uint y: range(X.size.y)) {
   const ref<float> Xy = X.row(y);
   const mref<float> Yy = Y.slice(y);
   const uint W = Y.stride;
   float sum = 0;
   for(uint x: range(R)) sum += Xy[x];
   for(uint x: range(R)) {
    sum += Xy[x+R];
    Yy[x*W] = sum / (R+x+1);
   }
   const uint end = X.size.x-R;
   const float w = 1.f / (2*R+1);
   for(uint x=R; x<end; x++) {
    sum += Xy[x+R];
    Yy[x*W] = w * sum;
    sum -= Xy[x-R];
   }
   for(uint x: range(X.size.x-R, X.size.x)) {
    Yy[x*W] = sum / (R+(X.size.x-1-x)+1);
    sum -= Xy[x-R];
   }
  }
 }
}

const double Kb = 0.0722, Kr = 0.2126;
const double rv = (1-Kr)*255/112;
const double gu = (1-Kb)*Kb/(1-Kr-Kb)*255/112;
const double gv = (1-Kr)*Kr/(1-Kr-Kb)*255/112;
const double bu = (1-Kb)*255/112;

void sRGBfromBT709(const Image& target, const ImageF& Y, const ImageF& U, const ImageF& V) {
 assert_(target.size == Y.size && target.stride == Y.stride && Y.size == U.size && Y.size == V.size && Y.stride == U.stride && Y.stride == V.stride);
 for(size_t i: range(Y.ref::size)) {
  int y = (int(Y[i]) - 16)*255/219;
  int Cb = int(U[i]) - 128;
  int Cr = int(V[i]) - 128;
  int r = y                        + Cr*rv;
  int g = y - Cb*gu - Cr*gv;
  int b = y + Cb*bu;
  target[i] = byte4(clamp(0,b,255), clamp(0,g,255), clamp(0,r,255));
 }
}


// -- Convolution --

/// Convolves and transposes (with mirror border conditions)
void convolve(float* target, const float* source, const float* kernel, int radius, int width, int height, uint sourceStride, uint targetStride) {
 int N = radius+1+radius;
 assert_(N < 1024, N);
 //chunk_parallel(height, [=](uint, size_t y) {
 for(size_t y: range(height)) {
  const float* line = source + y * sourceStride;
  float* targetColumn = target + y;
  if(width >= radius+1) {
   for(int x: range(-radius,0)) {
    float sum = 0;
    for(int dx: range(N)) sum += kernel[dx] * line[abs(x+dx)];
    targetColumn[(x+radius)*targetStride] = sum;
   }
   for(int x: range(0,width-2*radius)) {
    float sum = 0;
    const float* span = line + x;
    for(int dx: range(N)) sum += kernel[dx] * span[dx];
    targetColumn[(x+radius)*targetStride] = sum;
   }
   assert_(width >= 2*radius);
   for(int x: range(width-2*radius,width-radius)){
    float sum = 0;
    for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(x+dx-(width-1))];
    targetColumn[(x+radius)*targetStride] = sum;
   }
  } else {
   for(int x: range(-radius, width-radius)) {
    float sum = 0;
    for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(abs(x+dx)-(width-1))];
    targetColumn[(x+radius)*targetStride] = sum;
   }
  }
 }
}

inline void operator*=(mref<float> values, float factor) { values.apply([factor](float v) { return factor*v; }, values); }

inline float exp(float x) { return __builtin_expf(x); }
inline float gaussian(float sigma, float x) { return exp(-sq(x/sigma)/2); }

void gaussianBlur(const ImageF& target, const ImageF& source, float sigma, int radius) {
 assert_(sigma > 0);
 if(!radius) radius = ceil(3*sigma);
 size_t N = radius+1+radius;
 assert_(int2(radius+1) <= source.size, sigma, radius, N, source.size);
 float kernel[N];
 for(int dx: range(N)) kernel[dx] = gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
 float sum = ::sum(ref<float>(kernel,N), 0.); assert_(sum, ref<float>(kernel,N)); mref<float>(kernel,N) *= 1/sum;
 buffer<float> transpose (target.height*target.width);
 convolve(transpose.begin(), source.begin(), kernel, radius, source.width, source.height, source.stride, source.height);
 assert_(source.size == target.size);
 convolve(target.begin(),  transpose.begin(), kernel, radius, target.height, target.width, target.height, target.stride);
}
