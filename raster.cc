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
    for(uint tileY=0; tileY<height; tileY++) for(uint tileX=0; tileX<width; tileX++) {
        Tile& tile = tiles[tileY*width+tileX];
        if(!tile.cleared) {
            if(tile.lastCleared) { //was already background on last frame (no need to regenerate)
                for(uint y=0;y<16;y++) for(uint x=0;x<16;x++) {
                    uint tx = x0+tileX*16+x, ty = y0+size.y-1-(tileY*16+y);
                    if(ty>=y0) ::framebuffer(tx,ty) = backgroundColor;
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
                if(!(tile.subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                    blue = tile.blue[pixelPtr/16][pixelPtr%16] * 255.f;
                    green = tile.green[pixelPtr/16][pixelPtr%16] * 255.f;
                    red = tile.red[pixelPtr/16][pixelPtr%16] * 255.f;
                } else {
                    blue = sum16(tile.subblue[pixelPtr]) * 255.f/(4*4);
                    green =  sum16(tile.subgreen[pixelPtr]) * 255.f/(4*4);
                    red =  sum16(tile.subred[pixelPtr]) * 255.f/(4*4);
                }
                uint x = x0+(tileX*4+blockX)*4+pixelX, y = y0+size.y-1-((tileY*4+blockY)*4+pixelY);
                 if(y>=y0) ::framebuffer(x,y)=byte4(sRGB[uint(blue)%256],sRGB[uint(green)%256],sRGB[uint(red)%256],255);
            }
        }
    }
}
