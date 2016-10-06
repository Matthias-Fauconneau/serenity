#include "raster.h"

void RenderTarget::resolve(const Image& target) {
    assert_(size);
#if 0
    constexpr float scale = 0xFFF;
    extern uint8 sRGB_forward[0x1000];
#else
    constexpr float scale = 0xFF;
    struct { uint8 operator[](uint v) { return v; } } sRGB_forward;
#endif
    const byte4 backgroundColor(
                sRGB_forward[uint(this->backgroundColor.b*scale)],
                sRGB_forward[uint(this->backgroundColor.g*scale)],
                sRGB_forward[uint(this->backgroundColor.r*scale)], 0xFF);
    const uint stride = target.stride;
    for(uint tileY: range(height)) for(uint tileX: range(width)) {
        Tile& tile = tiles[tileY*width+tileX];
        byte4* const targetTilePtr = &target(tileX*16, tileY*16);
        if(!tile.cleared) { // Empty
            if(tile.lastCleared) for(uint y: range(16)) for(uint x: range(16)) targetTilePtr[y*stride+x] = backgroundColor;
            // else was already empty on last frame
            continue;
        }
        //static size_t block = 0, sample = 0;
        for(uint blockY: range(4)) for(uint blockX: range(4)) {
            const uint blockI = blockY*4+blockX;
            const uint blockPtr = blockI*(4*4);
            byte4* const targetBlockPtr = targetTilePtr+blockY*4*stride+blockX*4;
            //if(!tile.subsample[blockI]) block++; else sample++;
            if(!tile.subsample[blockI]) { // Fast (vectorized) path to convert full block of non-multisampled pixels
                v16sf blue = tile.blue[blockI]*scale;
                v16sf green = tile.green[blockI]*scale;
                v16sf red = tile.red[blockI]*scale;
#if 0
                // TODO: gather sRGB lookup or arithmetic convert
#else // no sRGB
                v16si B = cvtt(blue);
                v16si G = cvtt(green);
                v16si R = cvtt(red);
                for(uint pixelY: range(4)) for(uint pixelX: range(4)) {
                    const uint pixelI = pixelY*4+pixelX;
                    targetBlockPtr[pixelY*stride+pixelX] = byte4(B[pixelI], G[pixelI], R[pixelI], 0xFF);
                }
#endif
            } else { // Resolves (average samples of) multisampled pixels
                for(uint pixelY: range(4)) for(uint pixelX: range(4)) {
                    const uint pixelI = pixelY*4+pixelX;
                    const uint pixelPtr = blockPtr+pixelI;
                    float blue, green, red;
                    if(!(tile.subsample[blockI]&(1<<pixelI))) {
                        blue = ((float*)tile.blue)[pixelPtr] * scale;
                        green = ((float*)tile.green)[pixelPtr] * scale;
                        red = ((float*)tile.red)[pixelPtr] * scale;
                    } else {
                        blue = sum16(tile.subblue[pixelPtr]) * scale/(4*4);
                        green = sum16(tile.subgreen[pixelPtr]) * scale/(4*4);
                        red = sum16(tile.subred[pixelPtr]) * scale/(4*4);
                    }
                    targetBlockPtr[pixelY*stride+pixelX] = byte4(sRGB_forward[uint(blue)], sRGB_forward[uint(green)], sRGB_forward[uint(red)], 0xFF);
                }
            }
        }
        //log(block, sample, (float)block/sample);
    }
}
