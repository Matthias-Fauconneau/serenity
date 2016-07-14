#include "cr2.h"
#include "png.h"
#include "time.h"

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
    log(cr2.whiteBalance.R, cr2.whiteBalance.G, cr2.whiteBalance.B);
    const Image16& image = cr2.image;
    Image32 R (image.size/2), G (image.size/2), B (image.size/2); // Linear
    //cr2.whiteBalance.R = 1, cr2.whiteBalance.G = 1, cr2.whiteBalance.B = 1;
    uint scaleR = 2*cr2.whiteBalance.R, scaleG = cr2.whiteBalance.G, scaleB = 2*cr2.whiteBalance.B;
    scaleR = 2, scaleG = 1, scaleB = 2;
    for(size_t y: range(image.height/2)) {
     for(size_t x: range(image.width/2)) {
      R(x, y) = uint(image(x*2+0, y*2+0))*scaleR;
      G(x, y) = (uint(image(x*2+1, y*2+0))+uint(image(x*2+0, y*2+1)))*scaleG;
      B(x, y) = uint(image(x*2+1, y*2+1))*scaleB;
     }
    }
    Image sRGB (image.size/2);
    uint minR = -1, minG = -1, minB = -1, max = 0;
    //uint64 sumR = 0, sumG = 0, sumB = 0;
    //for(size_t i: range(R.ref::size)) { minR=::min(minR,R[i]), max=::max(max,R[i]); sumR += R[i]; }
    //for(size_t i: range(G.ref::size)) { minG=::min(minG,G[i]), max=::max(max,G[i]); sumG += G[i]; }
    //for(size_t i: range(B.ref::size)) { minB=::min(minB,B[i]), max=::max(max,B[i]); sumB += B[i]; }
    for(size_t y: range(R.height/4, 3*R.height/4)) for(size_t x: range(R.width/4, 3*R.width/4)) { minR=::min(minR,R(x,y)), max=::max(max,R(x,y)); }
    for(size_t y: range(G.height/4, 3*G.height/4)) for(size_t x: range(G.width/4, 3*G.width/4)) { minG=::min(minG,G(x,y)), max=::max(max,G(x,y)); }
    for(size_t y: range(B.height/4, 3*B.height/4)) for(size_t x: range(B.width/4, 3*B.width/4)) { minB=::min(minB,B(x,y)), max=::max(max,B(x,y)); }
    //for(size_t i: range(2*G.width, G.ref::size)) { minG=::min(minG,G[i]), max=::max(max,G[i]); }
    //for(size_t i: range(2*B.width, B.ref::size)) { minB=::min(minB,B[i]), max=::max(max,B[i]); }
    log(minB, minG, minR, max);
    //minB = minG = minR = 1963;
    {uint scaleR = cr2.whiteBalance.R, scaleG = cr2.whiteBalance.G, scaleB = cr2.whiteBalance.B;
    for(size_t i: range(R.ref::size)) R[i] = ::max(0,int(R[i]-minR))*scaleR;
    for(size_t i: range(G.ref::size)) G[i] = ::max(0,int(G[i]-minG))*scaleG;
    for(size_t i: range(B.ref::size)) B[i] = ::max(0,int(B[i]-minB))*scaleB;}
#if 0
    uint64 sumR = 0, sumG = 0, sumB = 0;
    for(size_t i: range(R.ref::size)) { sumR += ::max(0,int(R[i]-minR)); }
    for(size_t i: range(G.ref::size)) { sumG += ::max(0,int(G[i]-minG)); }
    for(size_t i: range(B.ref::size)) { sumB += ::max(0,int(B[i]-minB)); }
    //log(sumR, sumG, sumB);
    unused uint meanR = sumR / R.ref::size, meanG = sumG  / G.ref::size, meanB = sumB / B.ref::size;
    //meanR = max(1,meanR), meanG = max(1, meanG), meanB = max(1,meanB);
#endif
    max = 0;
    for(size_t i: range(R.ref::size)) { max=::max(max,R[i]); }
    for(size_t i: range(G.ref::size)) { max=::max(max,G[i]); }
    for(size_t i: range(B.ref::size)) { max=::max(max,B[i]); }
    //max -= min; //assert_(min == 0, min, max);

    extern uint8 sRGB_forward[0x1000];
    //log(meanB, meanG, meanR);
    /*uint denB = (max-minB)/0xFFF;
    uint denG = (max-minG)/0xFFF;
    uint denR = (max-minR)/0xFFF;*/
    //max = 1<<22;
    uint denB = (max)/0xFFF;
    uint denG = (max)/0xFFF;
    uint denR = (max)/0xFFF;
    log(denB, denG, denR, max);
    for(size_t i: range(sRGB.ref::size))
#if 0
     sRGB[i] = byte4(
       sRGB_forward[min<uint>(0xFFF,::max(0,int(B[i]-minB))*0x800/meanB)],
       sRGB_forward[min<uint>(0xFFF,::max(0,int(G[i]-minG))*0x800/meanG)],
       sRGB_forward[min<uint>(0xFFF,::max(0,int(R[i]-minR))*0x800/meanR)], 0xFF);
#elif 0
     sRGB[i] = byte4(
       sRGB_forward[min<uint>(0xFFF,::max(0,int(B[i]-minB))/denB)],
       sRGB_forward[min<uint>(0xFFF,::max(0,int(G[i]-minG))/denG)],
       sRGB_forward[min<uint>(0xFFF,::max(0,int(R[i]-minR))/denR)], 0xFF);
#else
    sRGB[i] = byte4(
      sRGB_forward[min<uint>(0xFFF,::max(0,int(B[i]))/denB)],
      sRGB_forward[min<uint>(0xFFF,::max(0,int(G[i]))/denG)],
      sRGB_forward[min<uint>(0xFFF,::max(0,int(R[i]))/denR)], 0xFF);
#endif
    //for(size_t i: range(sRGB.ref::size)) sRGB[i] = byte4(B[i]>>(12-8), G[i]>>(12-8), R[i]>>(12-8), 0xFF);
    //for(size_t i: range(sRGB.ref::size)) sRGB[i] = byte4((B[i]-minB)*0xFF/(max-minB), (G[i]-minG)*0xFF/(max-minG), (R[i]-minR)*0xFF/(max-minR), 0xFF);
    //for(size_t i: range(image.ref::size)) sRGB[i] = byte3((int(image[i])-min)*0xFF/(max-min));
    Time encode {true};
    auto png = encodePNG(sRGB);
    log(encode);
    writeFile(name+".png"_, png, currentWorkingDirectory(), true);
    log(total);
    break;
   }
 }
} app;
