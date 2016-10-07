#include "raster.h"

// Untiles render buffer, resolves (average samples of) multisampled pixels, and converts to half floats (linear RGB)
void RenderTarget::resolve(const ImageH& B, const ImageH& G, const ImageH& R) {
    const uint stride = B.stride;
    const v16si pixelSeq = (4*4)*seqI;
    for(uint tileY: range(height)) for(uint tileX: range(width)) {
        Tile& tile = tiles[tileY*width+tileX];
        uint const targetTilePtr = tileY*16*stride + tileX*16;
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
            v16sf blue, green, red;
            const mask16 subsample = tile.subsample[blockI];
            if(!subsample) { // No multisampled pixel in block => Directly load block without any multisampled pixels to blend
                blue = tile.blue[blockI], green = tile.green[blockI], red = tile.red[blockI];
            } else {
                // Resolves (average samples of) multisampled pixels
                v16sf blueSum = v16sf(0), greenSum = v16sf(0), redSum = v16sf(0);
                for(uint sampleI: range(4*4)) {
                    blueSum += gather((float*)(tile.subblue+blockPtr)+sampleI, pixelSeq);
                    greenSum += gather((float*)(tile.subgreen+blockPtr)+sampleI, pixelSeq);
                    redSum += gather((float*)(tile.subred+blockPtr)+sampleI, pixelSeq);
                }
                const v16sf scale = v16sf(1./(4*4));
                blue = blend(tile.blue[blockI], scale*blueSum, mask(subsample));
                green = blend(tile.green[blockI], scale*greenSum, mask(subsample));
                red = blend(tile.red[blockI], scale*redSum, mask(subsample));
            }
            // Converts to half and untiles block of pixels
            v16hf b = toHalf(blue);
            v16hf g = toHalf(green);
            v16hf r = toHalf(red);
#if 0
            *(v16hf*)(B.data+targetTilePtr+blockI*16) = b;
            *(v16hf*)(G.data+targetTilePtr+blockI*16) = g;
            *(v16hf*)(R.data+targetTilePtr+blockI*16) = r;
#else
            const uint targetBlockPtr = targetTilePtr+blockY*4*stride+blockX*4;
            *(v4hf*)(B.data+targetBlockPtr+0*stride) = __builtin_shufflevector(b, b, 0*4+0, 0*4+1, 0*4+2, 0*4+3);
            *(v4hf*)(G.data+targetBlockPtr+0*stride) = __builtin_shufflevector(g, g, 0*4+0, 0*4+1, 0*4+2, 0*4+3);
            *(v4hf*)(R.data+targetBlockPtr+0*stride) = __builtin_shufflevector(r, r, 0*4+0, 0*4+1, 0*4+2, 0*4+3);
            *(v4hf*)(B.data+targetBlockPtr+1*stride) = __builtin_shufflevector(b, b, 1*4+0, 1*4+1, 1*4+2, 1*4+3);
            *(v4hf*)(G.data+targetBlockPtr+1*stride) = __builtin_shufflevector(g, g, 1*4+0, 1*4+1, 1*4+2, 1*4+3);
            *(v4hf*)(R.data+targetBlockPtr+1*stride) = __builtin_shufflevector(r, r, 1*4+0, 1*4+1, 1*4+2, 1*4+3);
            *(v4hf*)(B.data+targetBlockPtr+2*stride) = __builtin_shufflevector(b, b, 2*4+0, 2*4+1, 2*4+2, 2*4+3);
            *(v4hf*)(G.data+targetBlockPtr+2*stride) = __builtin_shufflevector(g, g, 2*4+0, 2*4+1, 2*4+2, 2*4+3);
            *(v4hf*)(R.data+targetBlockPtr+2*stride) = __builtin_shufflevector(r, r, 2*4+0, 2*4+1, 2*4+2, 2*4+3);
            *(v4hf*)(B.data+targetBlockPtr+3*stride) = __builtin_shufflevector(b, b, 3*4+0, 3*4+1, 3*4+2, 3*4+3);
            *(v4hf*)(G.data+targetBlockPtr+3*stride) = __builtin_shufflevector(g, g, 3*4+0, 3*4+1, 3*4+2, 3*4+3);
            *(v4hf*)(R.data+targetBlockPtr+3*stride) = __builtin_shufflevector(r, r, 3*4+0, 3*4+1, 3*4+2, 3*4+3);
#endif
        }
    }
}

void convert(const Image& target, const ImageH& B, const ImageH& G, const ImageH& R) {
    extern uint8 sRGB_forward[0x1000];
    for(size_t i: range(target.ref::size)) {
        target[i] = byte4(sRGB_forward[uint(B[i]*0xFFF)], sRGB_forward[uint(G[i]*0xFFF)], sRGB_forward[uint(R[i]*0xFFF)], 0xFF);
    }
}
