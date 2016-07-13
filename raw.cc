#include "thread.h"
#include "data.h"
#include "png.h"
#include "cr2.h"

struct Raw {
 Raw() {
  for(string name: Folder(".").list(Files|Sorted))
   if(endsWith(toLower(name), ".cr2")) {
    log(name);
    Image16 image = decodeCR2(Map(name));
    Image sRGB (image.size);
    assert_(sRGB.stride == image.stride);
    int min = 0xFFFF, max = 0;
    for(size_t i: range(image.ref::size)) min=::min(min,int(image[i])), max=::max(max,int(image[i]));
    //for(size_t i: range(image.ref::size)) sRGB[i] = byte3(image[i] >> (12-8));
    for(size_t i: range(image.ref::size)) sRGB[i] = byte3((int(image[i])-min)*0xFF/(max-min));
    writeFile(name+".png"_,encodePNG(sRGB), currentWorkingDirectory(), true);
    break;
   }
 }
} app;
