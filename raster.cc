#include "raster.h"

#if 0
// Untiles render buffer, resolves (average samples of) multisampled pixels, and converts to half floats (linear RGB)
void RenderTarget::resolve(const ImageH& targetZ, const ImageH& targetB, const ImageH& targetG, const ImageH& targetR) {
    const uint stride = targetZ.stride;
    const v16si pixelSeq = (4*4)*seqI;
    for(uint tileY: range(height)) for(uint tileX: range(width)) {
        Tile& tile = tiles[tileY*width+tileX];
        uint const targetTilePtr = tileY*16*stride + tileX*16;
        if(tile.needClear) { // Empty
            for(uint y: range(16)) for(uint x: range(16)) {
                targetZ[targetTilePtr+y*stride+x] = this->Z;
                targetB[targetTilePtr+y*stride+x] = backgroundColor.b;
                targetG[targetTilePtr+y*stride+x] = backgroundColor.g;
                targetR[targetTilePtr+y*stride+x] = backgroundColor.r;
            }
            continue;
        }
        for(uint blockY: range(4)) for(uint blockX: range(4)) {
            const uint blockI = blockY*4+blockX;
            const uint blockPtr = blockI*(4*4);
            v16sf Z, B, G, R;
            const mask16 subsample = tile.subsample[blockI];
            if(!subsample) { // No multisampled pixel in block => Directly load block without any multisampled pixels to blend
                Z = tile.Z[blockI], B = tile.B[blockI], G = tile.G[blockI], R = tile.R[blockI];
            } else {
                // Resolves (average samples of) multisampled pixels
                v16sf zSum = v16sf(0), bSum = v16sf(0), gSum = v16sf(0), rSum = v16sf(0);
                for(uint sampleI: range(4*4)) {
                    zSum += gather((float*)(tile.subZ+blockPtr)+sampleI, pixelSeq);
                    bSum += gather((float*)(tile.subB+blockPtr)+sampleI, pixelSeq);
                    gSum += gather((float*)(tile.subG+blockPtr)+sampleI, pixelSeq);
                    rSum += gather((float*)(tile.subR+blockPtr)+sampleI, pixelSeq);
                }
                const v16sf scale = v16sf(1./(4*4));
                Z = blend(tile.Z[blockI], scale*zSum, mask(subsample));
                B = blend(tile.B[blockI], scale*bSum, mask(subsample));
                G = blend(tile.G[blockI], scale*gSum, mask(subsample));
                R = blend(tile.R[blockI], scale*rSum, mask(subsample));
            }
            // Converts to half and untiles block of pixels
            v16hf z = toHalf(Z);
            v16hf b = toHalf(B);
            v16hf g = toHalf(G);
            v16hf r = toHalf(R);
#if 0
            *(v16hf*)(Z.data+targetTilePtr+blockI*16) = z;
            *(v16hf*)(B.data+targetTilePtr+blockI*16) = b;
            *(v16hf*)(G.data+targetTilePtr+blockI*16) = g;
            *(v16hf*)(R.data+targetTilePtr+blockI*16) = r;
#else
            const uint targetBlockPtr = targetTilePtr+blockY*4*stride+blockX*4;
            *(v4hf*)(targetZ.data+targetBlockPtr+0*stride) = __builtin_shufflevector(z, z, 0*4+0, 0*4+1, 0*4+2, 0*4+3);
            *(v4hf*)(targetB.data+targetBlockPtr+0*stride) = __builtin_shufflevector(b, b, 0*4+0, 0*4+1, 0*4+2, 0*4+3);
            *(v4hf*)(targetG.data+targetBlockPtr+0*stride) = __builtin_shufflevector(g, g, 0*4+0, 0*4+1, 0*4+2, 0*4+3);
            *(v4hf*)(targetR.data+targetBlockPtr+0*stride) = __builtin_shufflevector(r, r, 0*4+0, 0*4+1, 0*4+2, 0*4+3);
            *(v4hf*)(targetZ.data+targetBlockPtr+1*stride) = __builtin_shufflevector(z, z, 1*4+0, 1*4+1, 1*4+2, 1*4+3);
            *(v4hf*)(targetB.data+targetBlockPtr+1*stride) = __builtin_shufflevector(b, b, 1*4+0, 1*4+1, 1*4+2, 1*4+3);
            *(v4hf*)(targetG.data+targetBlockPtr+1*stride) = __builtin_shufflevector(g, g, 1*4+0, 1*4+1, 1*4+2, 1*4+3);
            *(v4hf*)(targetR.data+targetBlockPtr+1*stride) = __builtin_shufflevector(r, r, 1*4+0, 1*4+1, 1*4+2, 1*4+3);
            *(v4hf*)(targetZ.data+targetBlockPtr+2*stride) = __builtin_shufflevector(z, z, 2*4+0, 2*4+1, 2*4+2, 2*4+3);
            *(v4hf*)(targetB.data+targetBlockPtr+2*stride) = __builtin_shufflevector(b, b, 2*4+0, 2*4+1, 2*4+2, 2*4+3);
            *(v4hf*)(targetG.data+targetBlockPtr+2*stride) = __builtin_shufflevector(g, g, 2*4+0, 2*4+1, 2*4+2, 2*4+3);
            *(v4hf*)(targetR.data+targetBlockPtr+2*stride) = __builtin_shufflevector(r, r, 2*4+0, 2*4+1, 2*4+2, 2*4+3);
            *(v4hf*)(targetZ.data+targetBlockPtr+3*stride) = __builtin_shufflevector(z, z, 3*4+0, 3*4+1, 3*4+2, 3*4+3);
            *(v4hf*)(targetB.data+targetBlockPtr+3*stride) = __builtin_shufflevector(b, b, 3*4+0, 3*4+1, 3*4+2, 3*4+3);
            *(v4hf*)(targetG.data+targetBlockPtr+3*stride) = __builtin_shufflevector(g, g, 3*4+0, 3*4+1, 3*4+2, 3*4+3);
            *(v4hf*)(targetR.data+targetBlockPtr+3*stride) = __builtin_shufflevector(r, r, 3*4+0, 3*4+1, 3*4+2, 3*4+3);
#endif
        }
    }
}
#endif

void convert(const Image& target, const ImageH& B, const ImageH& G, const ImageH& R) {
    assert_(target.size == B.size);
    extern uint8 sRGB_forward[0x1000];
    for(size_t i: range(target.ref::size)) {
        B[i] = clamp(0.f, (float)B[i], 1.f);
        G[i] = clamp(0.f, (float)G[i], 1.f);
        R[i] = clamp(0.f, (float)R[i], 1.f);
        assert_(B[i] >= 0 && B[i] <= 1, (float)B[i]);
        assert_(G[i] >= 0 && G[i] <= 1, (float)G[i]);
        assert_(R[i] >= 0 && R[i] <= 1, (float)R[i]);
        target[i] = byte4(sRGB_forward[uint(B[i]*0xFFF)], sRGB_forward[uint(G[i]*0xFFF)], sRGB_forward[uint(R[i]*0xFFF)], 0xFF);
    }
}
