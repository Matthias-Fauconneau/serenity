#include "cr2.h"
#include "png.h"
#include "time.h"
#include "matrix.h"
inline double log2(double x) { return __builtin_log2(x); }

/// 2D array of 32bit integer pixels
typedef ImageT<uint32> Image32;

mat3 pseudoinverse(mat3 in) {
 mat3 out;
 float w[3][6];
 for(int i: range(3)) {
  for(int j: range(6)) w[i][j] = (j == i+3);
  for(int j: range(3)) for(int k: range(3))
   w[i][j] += in[k][i] * in[k][j];
 }
 for(int i: range(3)) {
  float sum = w[i][i];
  for(int j: range(6)) w[i][j] /= sum;
  for(int k: range(3)) {
   if (k==i) continue;
   float wki = w[k][i];
   for(int j: range(6)) w[k][j] -= w[i][j] * wki;
  }
 }
 for(int i: range(3)) {
  for(int j: range(3)) {
   out[i][j] = 0;
   for(int k: range(3)) out[i][j] += w[j][k+3] * in[i][k];
  }
 }
 return out;
}


struct Raw {
 Raw() {
  size_t totalSize = 0, huffmanSize = 0, entropySize = 0;
  for(string name: Folder(".").list(Files|Sorted))
   if(endsWith(toLower(name), ".cr2")) {
    if(name != "IMG_1729.CR2") continue;
    //Time total {true};
    Map map(name);
    //Time decode {true};
    constexpr bool onlyParse = false;
    CR2 cr2(map, onlyParse);
    //log(decode);
    totalSize += map.size;
    huffmanSize += cr2.huffmanSize;
    if(onlyParse) continue;
    const Image16& image = cr2.image;
    Image16 planes[4]; // R, G1, G2, B
    for(size_t i: range(4)) planes[i] = Image16(image.size/2);
    for(size_t y: range(image.size.y/2)) for(size_t x: range(image.size.y/2)) {
     planes[0](x,y) = image(x*2+0, y*2+0);
     planes[1](x,y) = image(x*2+1, y*2+0);
     planes[2](x,y) = image(x*2+0, y*2+1);
     planes[3](x,y) = image(x*2+1, y*2+1);
    }
    double entropyCoded = 0;
    for(size_t i: range(4)) {
     const Image16& plane = planes[i];
     int16 predictor = 0;
     for(int16& value: plane) {
      int16 next = value-predictor;
      predictor = value;
      value = next;
     }
     int16 min = 0x7FFF, max = 0;
     for(int16 value: plane) { min=::min(min, value); max=::max(max, value); }
     //log(min, max);
     assert_(max+1-min <= 46980, min, max, max+1-min);
     buffer<uint32> histogram(max+1-min);
     histogram.clear(0);
     uint32* base = histogram.begin()-min;
     for(int16 value: plane) base[value]++;
     //uint32 maxCount = 0; for(uint32 count: histogram) maxCount=::max(maxCount, count); log(maxCount);
     const uint32 total = plane.ref::size;
     //log("Uniform", str(total*log2(double(max-min))/8/1024/1024, 0u), "MB");
     for(uint32 count: histogram) if(count) entropyCoded += count * log2(double(total)/double(count));
     entropyCoded += histogram.size*8;
    }
    log(name, map.size/1024/1024,"MB","Huffman",cr2.huffmanSize/1024/1024,"MB","Entropy", str(entropyCoded/8/1024/1024,0u),"MB");
    entropySize += entropyCoded/8;
#if 0
    uint min = 511, max = 4000;
    //min=2000; //min=-1; for(uint v: image) min=::min(min, v); log(min);
    uint minR = -1, minG1 = -1, minG2 = -1, minB = -1;
    for(size_t Y: range(image.height/2/8, 7*image.height/2/8)) for(size_t X: range(image.width/2/8, 7*image.width/2/8)) {
     size_t y = 2*Y, x = 2*X;
     uint R = image(x+0,y+0), G1 = image(x+1,y+0), G2 = image(x+0,y+1), B = image(x+1,y+1);
     minR=::min(minR, R);
     minG1=::min(minG1, G1);
     minG2=::min(minG2, G2);
     minB=::min(minB, B);
    }
    min = ::min(::min(::min(minR, minG1), minG2), minB);

    mat3 cam_xyz {
      vec3(0.9602, -0.2984, -0.0407),
      vec3(-0.3823, 1.1495,  0.1415),
      vec3(-0.0937, 0.1675, 0.5049)};
    mat3 xyz_rgb {
        vec3( 0.412453, 0.212671, 0.019334 ),
        vec3( 0.357580, 0.715160, 0.119193 ),
        vec3( 0.180423, 0.072169, 0.950227 ) };
    //cr2.whiteBalance.R = 1, cr2.whiteBalance.G = 1, cr2.whiteBalance.B = 1;
    //uint maxBalance = ::max(::max(cr2.whiteBalance.R, cr2.whiteBalance.G), cr2.whiteBalance.B);
    //vec3 whiteBalance ( (float)cr2.whiteBalance.R/maxBalance, (float)cr2.whiteBalance.G/(maxBalance*2), (float)cr2.whiteBalance.B/maxBalance );
    mat3 cam_rgb = cam_xyz*xyz_rgb;
    for(int i: range(3)) { // Normalize rows by sum
     float sum = 0;
     for(int j: range(3)) sum += cam_rgb[i][j];
     for(int j: range(3)) cam_rgb(i, j) /= sum;
     //pre_mul[i] = 1 / num;
    }
    mat3 m = pseudoinverse(cam_rgb);//*mat3(whiteBalance); // rgb16_cam (* max)
    //uint64 m00 = m(0,0), m01 = m(0, 1), m02 = m(0, 2);
    //uint64 m10 = m(1,0), m11 = m(1, 1), m12 = m(1, 2);
    //uint64 m20 = m(2,0), m21 = m(2, 1), m22 = m(2, 2);
    //m = mat3(1);
    log(m);
    m = 0xFFFF*m;

    size_t cropY = 18, cropX = 96;
    int2 size ((image.width-cropX)/2, (image.height-cropY)/2);
    ImageF R (size), G (size), B (size); // Linear sRGB
    for(size_t Y: range(size.y)) {
     for(size_t X: range(size.x)) {
      size_t y = cropY+2*Y, x = cropX+2*X;
      uint r = image(x+0,y+0), g1 = image(x+1,y+0), g2 = image(x+0,y+1), b = image(x+1,y+1);
#if 0
      uint cR = ::max(0,int(r-min));
      uint cG = (::max(0,int(g1-min)) + ::max(0,int(g2-min)))/2;
      uint cB = ::max(0,int(b-min));
#else
      float cR = (float)r-min;
      float cG = ((float)g1-min + (float)g2-min)/2.f;
      float cB = (float)b-min;
#endif
      vec3 rgb = m * (vec3(cR,cG,cB)/float(max));
      R(X, Y) = ::max(0.f, rgb[0]);
      G(X, Y) = ::max(0.f, rgb[1]);
      B(X, Y) = ::max(0.f, rgb[2]);
      //R(X, Y) = (m00 * cR + m01 * cG + m02 * cB)/max;
      //G(X, Y) = (m10 * cR + m11 * cG + m12 * cB)/max;
      //B(X, Y) = (m20 * cR + m21 * cG + m22 * cB)/max;
     }
    }
    extern uint8 sRGB_forward[0x1000];
    Image sRGB (size);
    for(size_t i: range(sRGB.ref::size))
#if 0
     sRGB[i] = byte4(
        B[i]>>(16-8),
        G[i]>>(16-8),
        R[i]>>(16-8), 0xFF);
#elif 1
     sRGB[i] = byte4(
      sRGB_forward[::min<uint>(0xFFF, B[i]/(1<<(16-12)))],
      sRGB_forward[::min<uint>(0xFFF, G[i]/(1<<(16-12)))],
      sRGB_forward[::min<uint>(0xFFF, R[i]/(1<<(16-12)))], 0xFF);
#else
     sRGB[i] = byte4(
      sRGB_forward[::min<uint>(0xFFF, B[i]>>(16-12))],
      sRGB_forward[::min<uint>(0xFFF, G[i]>>(16-12))],
      sRGB_forward[::min<uint>(0xFFF, R[i]>>(16-12))], 0xFF);
#endif
    Time encode {true};
    auto png = encodePNG(sRGB);
    log(encode);
    writeFile(name+".png"_, png, currentWorkingDirectory(), true);
#endif
    //log(total);
    //break;
    log(totalSize/1024/1024,"MB -", entropySize/1024/1024,"MB =", (totalSize-entropySize)/1024/1024,"MB", 100*(totalSize-entropySize)/totalSize,"%");
   }
  //log(totalSize/1024/1024,"MB -", compressedSize/1024/1024,"MB =", (totalSize-compressedSize)/1024/1024,"MB", 100*(totalSize-compressedSize)/compressedSize,"%");
  //log(huffmanSize/1024/1024,"MB -", entropySize/1024/1024,"MB =", (huffmanSize-entropySize)/1024/1024,"MB", 100*(huffmanSize-entropySize)/totalSize,"%");
  log(totalSize/1024/1024,"MB -", entropySize/1024/1024,"MB =", (totalSize-entropySize)/1024/1024,"MB", 100*(totalSize-entropySize)/totalSize,"%");
 }
} app;
