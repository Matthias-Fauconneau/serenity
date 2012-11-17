#include "raster.h"
#include "display.h"

struct sRGB {
    uint8 lookup[256];
    inline float evaluate(float c) { if(c>=0.0031308) return 1.055*pow(c,1/2.4)-0.055; else return 12.92*c; }
    sRGB() { for(uint i=0;i<256;i++) { uint l = round(255*evaluate(i/255.f)); assert(l<256); lookup[i]=l; } }
    inline uint8 operator [](uint c) { assert(c<256,c); return lookup[c]; }
} sRGB;

void RenderTarget::resolve(int2 position, int2 size) {
    const byte4 backgroundColor(sRGB[uint(blue*255.f)%256],sRGB[uint(green*255.f)%256],sRGB[uint(red*255.f)%256],255);
    uint x0=position.x, y0=position.y;
    for(uint binY=0; binY<height; binY++) for(uint binX=0; binX<width; binX++) {
        Bin& bin = bins[binY*width+binX];
        if(!bin.cleared) {
            if(bin.lastCleared) { //was already background on last frame (no need to regenerate)
                for(uint y=0;y<16;y++) for(uint x=0;x<16;x++) {
                    uint tx = x0+binX*16+x, ty = y0+size.y-1-(binY*16+y);
                    ::framebuffer(tx,ty) = backgroundColor;
                }
            }
            continue;
        }
        for(uint blockY=0; blockY<4; blockY++) for(uint blockX=0; blockX<4; blockX++) {
            uint blockI = blockY*4+blockX;
            const uint blockPtr = blockI*(4*4);
            for(uint pixelY=0; pixelY<4; pixelY++) for(uint pixelX=0; pixelX<4; pixelX++) { //TODO: vectorize
                uint pixelI = pixelY*4+pixelX;
                const uint pixelPtr = blockPtr+pixelI;
                float blue, green, red;
                if(!(bin.subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                    blue = bin.blue[pixelPtr/16][pixelPtr%16] * 255.f;
                    green = bin.green[pixelPtr/16][pixelPtr%16] * 255.f;
                    red = bin.red[pixelPtr/16][pixelPtr%16] * 255.f;
                } else {
                    blue = sum16(bin.subblue[pixelPtr]) * 255.f/(4*4);
                    green =  sum16(bin.subgreen[pixelPtr]) * 255.f/(4*4);
                    red =  sum16(bin.subred[pixelPtr]) * 255.f/(4*4);
                }
                uint x = x0+(binX*4+blockX)*4+pixelX, y = y0+size.y-1-((binY*4+blockY)*4+pixelY);
                ::framebuffer(x,y)=byte4(sRGB[uint(blue)%256],sRGB[uint(green)%256],sRGB[uint(red)%256],255);
            }
        }
    }
}
