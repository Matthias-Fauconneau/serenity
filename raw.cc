#include "cr2.h"
#include "png.h"
#include "time.h"
#include "matrix.h"

/// 2D array of 32bit integer pixels
typedef ImageT<uint32> Image32;

struct Raw {
 Raw() {
  for(string name: Folder(".").list(Files|Sorted))
   if(endsWith(toLower(name), ".cr2")) {
    log(name);
    Time total {true};
    Map map(name);
    Time decode {true};
    CR2 cr2(map);
    log(decode);
    const Image16& image = cr2.image;
    uint minR = -1, minG1 = -1, minG2 = -1, minB = -1;
    uint maxR0 = 0, maxG1 = 0, maxG2 = 0, maxB0 = 0;
    for(size_t Y: range(image.height/2/8, 7*image.height/2/8)) for(size_t X: range(image.width/2/8, 7*image.width/2/8)) {
     size_t y = 2*Y, x = 2*X;
     uint R = image(x+0,y+0), G1 = image(x+1,y+0), G2 = image(x+0,y+1), B = image(x+1,y+1);
     minR=::min(minR, R);
     minG1=::min(minG1, G1);
     minG2=::min(minG2, G2);
     minB=::min(minB, B);
     maxR0=::max(maxR0,R);
     maxG1=::max(maxG1,G1);
     maxG2=::max(maxG2,G2);
     maxB0=::max(maxB0,B);
    }
    log(minR, minG1, minG2, minB);
    //minR = 511, minG1 = 511, minG2 = 511, minB = 511;
    log(maxR0, maxG1, maxG2, maxB0);
    log(cr2.whiteBalance.R, cr2.whiteBalance.G, cr2.whiteBalance.B);
    uint max = 4000;
    cr2.whiteBalance.R = 1, cr2.whiteBalance.G = 1, cr2.whiteBalance.B = 1;
    uint maxBalance = ::max(::max(cr2.whiteBalance.R, cr2.whiteBalance.G), cr2.whiteBalance.B);
    uint64 _2_32 = (1ul<<32)-1;
    uint scaleR = _2_32*cr2.whiteBalance.R/(maxBalance*max), scaleG = _2_32*cr2.whiteBalance.G/(maxBalance*max*2), scaleB = _2_32*cr2.whiteBalance.B/(maxBalance*max);
    log(scaleR, scaleG, scaleB); // 33-12 = 21 +12 = 33
    log(max*scaleR, max*scaleG, max*scaleB);
    uint maxR = 0, maxG = 0, maxB = 0;
    size_t cropY = 18, cropX = 96;
    int2 size ((image.width-cropX)/2, (image.height-cropY)/2);

    /*mat3 xyz {
      vec3(0.9602, -0.3823, -0.0937),
      vec3(-0.2984, 1.1495,  0.1675),
      vec3(-0.0407, 0.1415, 0.5049)};*/
    mat3 xyz {
      vec3(0.9602, -0.3823, -0.0937),
      vec3(-0.2984, 1.1495,  0.1675),
      vec3(-0.0407, 0.1415, 0.5049)};
    mat3 xyz_rgb{			/* XYZ from RGB */
        { 0.412453, 0.357580, 0.180423 },
        { 0.212671, 0.715160, 0.072169 },
        { 0.019334, 0.119193, 0.950227 } };

    ImageF R (size), G (size), B (size); // Linear sRGB
    for(size_t Y: range(size.y)) {
     for(size_t X: range(size.x)) {
      size_t y = cropY+2*Y, x = cropX+2*X;
      uint r = image(x+0,y+0), g1 = image(x+1,y+0), g2 = image(x+0,y+1), b = image(x+1,y+1);
      uint sR = ::max(0,int(r-minR))*scaleR;
      uint sG = (::max(0,int(g1-minG1))+::max(0,int(g2-minG2)))*scaleG;
      uint sB = ::max(0,int(b-minB))*scaleB;
      maxR=::max(maxR,sR);
      maxG=::max(maxG,sG);
      maxB=::max(maxB,sB);
      R(X, Y) = sR;
      G(X, Y) = sG;
      B(X, Y) = sB;
     }
    }
    extern uint8 sRGB_forward[0x1000];
    Image sRGB (size);
    for(size_t i: range(sRGB.ref::size))
#if 0
     sRGB[i] = byte4(
        B[i]>>(32-8),
        G[i]>>(32-8),
        R[i]>>(32-8), 0xFF);
#elif 1
     sRGB[i] = byte4(
      sRGB_forward[min<uint>(0xFFF, B[i]>>(32-12))],
      sRGB_forward[min<uint>(0xFFF, G[i]>>(32-12))],
      sRGB_forward[min<uint>(0xFFF, R[i]>>(32-12))], 0xFF);
#else
     sRGB[i] = byte4(
      sRGB_forward[min<uint>(0xFFF, B[i]/(maxB/0xFFF))],
      sRGB_forward[min<uint>(0xFFF, G[i]/(maxG/0xFFF))],
      sRGB_forward[min<uint>(0xFFF, R[i]/(maxR/0xFFF))], 0xFF);
#endif
    Time encode {true};
    auto png = encodePNG(sRGB);
    log(encode);
    writeFile(name+".png"_, png, currentWorkingDirectory(), true);
    log(total);
    break;
   }
 }
} app;
