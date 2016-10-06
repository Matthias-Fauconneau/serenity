#include "raster.h"

void RenderTarget::resolve(const Image& target) {
    assert_(size);
    extern uint8 sRGB_forward[0x1000];
    const byte4 backgroundColor(sRGB_forward[uint(this->backgroundColor.b*0xFFF)],
            sRGB_forward[uint(this->backgroundColor.g*0xFFF)],
            sRGB_forward[uint(this->backgroundColor.r*0xFFF)], 0xFF);
    for(uint tileY=0; tileY<height; tileY++) for(uint tileX=0; tileX<width; tileX++) {
        Tile& tile = tiles[tileY*width+tileX];
        if(!tile.cleared) {
            if(tile.lastCleared) { // Was already background on last frame (no need to regenerate)
                for(uint y: range(16)) for(uint x: range(16)) {
                    uint tx = tileX*16+x, ty = /*size.y-1-*/(tileY*16+y);
                    target(tx, ty) = backgroundColor;
                    //for(uint sy: range(4)) for(uint sx: range(4)) target(tx*4+sx, ty*4+sy) = backgroundColor; // MSAA -> 4x
                }
            }
            continue;
        }
        for(uint blockY: range(4)) for(uint blockX: range(4)) {
            const uint blockI = blockY*4+blockX;
            const uint blockPtr = blockI*(4*4);
            for(uint pixelY: range(4)) for(uint pixelX: range(4)) { //TODO: vectorize
                const uint pixelI = pixelY*4+pixelX;
                const uint pixelPtr = blockPtr+pixelI;
                const uint x = (tileX*4+blockX)*4+pixelX, y = /*size.y-1-*/((tileY*4+blockY)*4+pixelY);
                float blue, green, red;
                if(!(tile.subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                    blue = tile.blue[pixelPtr/16][pixelPtr%16] * 0xFFF;
                    green = tile.green[pixelPtr/16][pixelPtr%16] * 0xFFF;
                    red = tile.red[pixelPtr/16][pixelPtr%16] * 0xFFF;
                    // MSAA -> 4x
                    /*for(uint sy: range(4)) for(uint sx: range(4))
                        target(x*4+sx,y*4+sy) = byte4(sRGB_forward[uint(blue)], sRGB_forward[uint(green)], sRGB_forward[uint(red)], 0xFF);*/
                } else {
                    blue = sum16(tile.subblue[pixelPtr]) * 0xFFF/(4*4);
                    green = sum16(tile.subgreen[pixelPtr]) * 0xFFF/(4*4);
                    red = sum16(tile.subred[pixelPtr]) * 0xFFF/(4*4);
                    /*for(uint sy: range(4)) for(uint sx: range(4)) {
                        blue = tile.subblue[pixelPtr][sy*4+sx] * 0xFFF;
                        green = tile.subgreen[pixelPtr][sy*4+sx] * 0xFFF;
                        red = tile.subred[pixelPtr][sy*4+sx] * 0xFFF;
                        target(x*4+sx,y*4+sy) = byte4(sRGB_forward[uint(blue)], sRGB_forward[uint(green)], sRGB_forward[uint(red)], 0xFF);
                    }*/
                }
                target(x,y) = byte4(sRGB_forward[uint(blue)], sRGB_forward[uint(green)], sRGB_forward[uint(red)], 0xFF);
            }
        }
    }
}
