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
    for(size_t y: range(image.height/2)) {
     for(size_t x: range(image.width/2)) {
      R(x, y) = uint(image(x*2+0, y*2+0))*2*cr2.whiteBalance.R;
      G(x, y) = (uint(image(x*2+1, y*2+0))+uint(image(x*2+0, y*2+1)))*cr2.whiteBalance.G;
      B(x, y) = uint(image(x*2+1, y*2+1))*2*cr2.whiteBalance.B;
     }
    }
    Image sRGB (image.size/2);
    uint minR = -1, minG = -1, minB = -1, max = 0;
    for(size_t i: range(R.ref::size)) minR=::min(minR,R[i]), max=::max(max,R[i]);
    for(size_t i: range(G.ref::size)) minG=::min(minG,G[i]), max=::max(max,G[i]);
    for(size_t i: range(B.ref::size)) minB=::min(minB,B[i]), max=::max(max,B[i]);
    //max -= min; //assert_(min == 0, min, max);
    //for(size_t i: range(sRGB.ref::size)) sRGB[i] = byte4(B[i]>>(12-8), G[i]>>(12-8), R[i]>>(12-8), 0xFF);
    for(size_t i: range(sRGB.ref::size)) sRGB[i] = byte4((B[i]-minB)*0xFF/(max-minB), (G[i]-minG)*0xFF/(max-minG), (R[i]-minR)*0xFF/(max-minR), 0xFF);
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
