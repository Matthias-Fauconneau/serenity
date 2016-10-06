#include "raster.h"

// Untiles render buffer, resolves (average samples of) multisampled pixels, and converts to half floats (linear RGB)
void RenderTarget::resolve(const ImageH& B, const ImageH& G, const ImageH& R) {
    const uint stride = B.stride;
    for(uint tileY: range(height)) for(uint tileX: range(width)) {
        Tile& tile = tiles[tileY*width+tileX];
        uint const targetTilePtr = tileY*16*stride + tileY*16;
        if(!tile.cleared) { // Empty
            if(tile.lastCleared) for(uint y: range(16)) for(uint x: range(16)) {
                B[targetTilePtr+y*stride+x] = backgroundColor.b;
                G[targetTilePtr+y*stride+x] = backgroundColor.g;
                R[targetTilePtr+y*stride+x] = backgroundColor.r;
            }
            // else was already empty on last frame
            continue;
        }
        for(uint blockY: range(4)) for(uint blockX: range(4)) {
            const uint blockI = blockY*4+blockX;
            const uint blockPtr = blockI*(4*4);
            const uint targetBlockPtr = targetTilePtr+blockY*4*stride+blockX*4;
            v16sf blue, green, red;
            if(!tile.subsample[blockI]) { // No multisampled pixel in block => Directly load block without any multisampled pixels to blend
                blue = tile.blue[blockI], green = tile.green[blockI], red = tile.red[blockI];
            } else {
                // Resolves (average samples of) multisampled pixels
                const v16si seqI (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
                const v16si pixelSeq = 16*seqI;
                v16sf blue = 0, green = 0, red = 0;
                for(uint sampleI: range(16)) {
                    blue += gather((float*)(tile.subblue+blockPtr)+sampleI, pixelSeq);
                    green += gather((float*)(tile.subgreen+blockPtr)+sampleI, pixelSeq);
                    red += gather((float*)(tile.subred+blockPtr)+sampleI, pixelSeq);
                }
                blend(tile.blue[blockI], blue, mask(tile.subsample[blockI]));
                blend(tile.green[blockI], green, mask(tile.subsample[blockI]));
                blend(tile.red[blockI], red, mask(tile.subsample[blockI]));
            }
            /*if(!tile.subsample[blockI])*/ { // Fast (vectorized) path to convert full block of non-multisampled pixels
                v16hf blue = toHalf(tile.blue[blockI]);
                v16hf green = toHalf(tile.green[blockI]);
                v16hf red = toHalf(tile.red[blockI]);
                for(uint pixelY: range(4)) for(uint pixelX: range(4)) { // FIXME: 4X
                    const uint pixelI = pixelY*4+pixelX;
                    const uint targetPixelI = targetBlockPtr+pixelY*stride+pixelX;
                    B[targetPixelI] = blue[pixelI];
                    G[targetPixelI] = green[pixelI];
                    R[targetPixelI] = red[pixelI];
                }
            } /*else { // Resolves (average samples of) multisampled pixels
                for(uint pixelY: range(4)) for(uint pixelX: range(4)) {
                    const uint pixelI = pixelY*4+pixelX;
                    const uint pixelPtr = blockPtr+pixelI;
                    const uint targetPixelI = targetBlockPtr+pixelY*stride+pixelX;
                    float blue, green, red;
                    if(!(tile.subsample[blockI]&(1<<pixelI))) {
                        blue = ((float*)tile.blue)[pixelPtr];
                        green = ((float*)tile.green)[pixelPtr];
                        red = ((float*)tile.red)[pixelPtr];
                    } else {
                        blue = sum16(tile.subblue[pixelPtr]) / (4*4);
                        green = sum16(tile.subgreen[pixelPtr]) / (4*4);
                        red = sum16(tile.subred[pixelPtr]) /(4*4);
                    }
                    B[targetPixelI] = blue;
                    G[targetPixelI] = green;
                    R[targetPixelI] = red;
                }
            }*/
        }
    }
}

void convert(const Image& target, const ImageH& B, const ImageH& G, const ImageH& R) {
    extern uint8 sRGB_forward[0x1000];
    for(size_t i: range(target.ref::size))
        target[i] = byte4(sRGB_forward[uint(B[i]*0xFFF)], sRGB_forward[uint(G[i]*0xFFF)], sRGB_forward[uint(R[i]*0xFFF)], 0xFF);
}
