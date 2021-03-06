/** \file raster.h 3D rasterizer
This rasterizer is an AVX implementation of a tile-based deferred renderer (cf http://www.drdobbs.com/parallel/rasterization-on-larrabee/217200602) :
Triangles are not immediatly rendered but first sorted in 16x16 pixels (64x64 samples) bins.
When all triangles have been setup, each tile is separately rendered.
Tiles can be processed in parallel (I'm using 4 hyperthreaded cores) and only access their local framebuffer (which should fit L1).
As presented in Abrash's article, rasterization is done recursively (4x4 blocks of 4x4 pixels of 4x4 samples) using precomputed step grids.
This architecture allows the rasterizer to leverage 16-wide vector units (on Ivy Bridge's 8-wide units, all operations have to be duplicated).
For each face, the rasterizer outputs pixel masks for each blocks (or sample masks for partial pixels).
Then, pixels are depth-tested, shaded and blended in the local framebuffer.
Finally, after all passes have been rendered, the tiles are resolved and copied to the application window buffer.
*/
#pragma once
#include "matrix.h"
#include "simd.h"
#include "image.h"
#include "parallel.h"

#define PROFILE 0
#if PROFILE
#include "time.h"
#define profile(s) s
#else
#define profile(s)
#endif

/// 64×64 pixels tile for L1 cache locality (64×64×RGBZ×float~64KB)
template<int C> struct Tile { // 64KB framebuffer (L1)
    v16sf pixelZ[4*4], pixels[C][4*4];
    v16sf sampleZ[16*16], samples[C][16*16];
    mask16 multisample[16]; // Per-pixel flag to trigger multisample operations
    bool needClear;
    Tile();
};

/// Tiled render target
template<int C> struct RenderTarget {
    int2 size = 0; // Pixels
    uint width = 0, height = 0; // Tiles
    buffer<Tile<C>> tiles;
    float clearZ; float clear[C];

    // Allocates all bins, flags them to be cleared
    void setup(int2 size, float clearZ, float clear[C]) {
        if(this->size != size) {
            this->size = size;
            width = align(64,size.x*4)/64;
            height = align(64,size.y*4)/64;
            tiles = buffer<Tile<C>>(width*height);
        }
        this->clearZ = clearZ;
        for(int c: range(C)) this->clear[c] = clear[c];
        for(Tile<C>& tile: tiles) { tile.needClear = true; } // Forces initial background blit to target
    }

    // Resolves internal MSAA linear framebuffer to linear half buffers
    void resolve(const ImageH& Z, const ImageH targets[C]);
};

// Untiles render buffer, resolves (average samples of) multisampled pixels, and converts to half floats (linear RGB)
template<int C> void RenderTarget<C>::resolve(const ImageH& Z, const ImageH targets[C]) {
    if(Z) {
        const uint stride = Z.stride;
        static constexpr v16si pixelSeq {v8si{(4*4)*0,(4*4)*1,(4*4)*2,(4*4)*3,(4*4)*4,(4*4)*5,(4*4)*6,(4*4)*7},v8si{(4*4)*8,(4*4)*9,(4*4)*10,(4*4)*11,(4*4)*12,(4*4)*13,(4*4)*14,(4*4)*15}};
        for(uint tileY: range(height)) for(uint tileX: range(width)) {
            Tile<C>& tile = tiles[tileY*width+tileX];
            uint const targetTilePtr = tileY*16*stride + tileX*16;
            if(tile.needClear) { // Empty
                for(uint y: range(16)) for(uint x: range(16)) {
                    Z[targetTilePtr+y*stride+x] = clearZ;
                    for(uint c: range(C)) targets[c][targetTilePtr+y*stride+x] = clear[c];
                }
                continue;
            }
            for(uint blockY: range(4)) for(uint blockX: range(4)) {
                const uint blockI = blockY*4+blockX;
                const uint blockPtr = blockI*(4*4);
                v16sf z, pixels[C];
                const mask16 multisample = tile.multisample[blockI];
                if(!multisample) { // No multisampled pixel in block => Directly load block without any multisampled pixels to blend
                    z = tile.pixelZ[blockI];
                    for(uint c: range(C)) pixels[c] = tile.pixels[c][blockI];
                } else {
                    // Resolves (average samples of) multisampled pixels
                    {
                        v16sf sum = v16sf(0);
                        for(uint sampleI: range(4*4)) sum += gather((float*)(tile.sampleZ+blockPtr)+sampleI, pixelSeq);
                        const v16sf scale = v16sf(1./(4*4));
                        z = blend(tile.pixelZ[blockI], scale*sum, mask(multisample));
                    }
                    for(uint c: range(C)) {
                        v16sf sum = v16sf(0);
                        for(uint sampleI: range(4*4)) sum += gather((float*)(tile.samples[c]+blockPtr)+sampleI, pixelSeq);
                        const v16sf scale = v16sf(1./(4*4));
                        pixels[c] = blend(tile.pixels[c][blockI], scale*sum, mask(multisample));
                    }
                }
                // Converts to half and untiles block of pixels
                const uint targetBlockPtr = targetTilePtr+blockY*4*stride+blockX*4;
                {
                    v16hf halfs = toHalf(z);
                    #define o(j) *(v4hf*)(Z.data+targetBlockPtr+j*stride) = __builtin_shufflevector(halfs, halfs, j*4+0, j*4+1, j*4+2, j*4+3);
                    o(0)o(1)o(2)o(3)
                    #undef o
                }
                for(uint c: range(C)) {
                    v16hf halfs = toHalf(pixels[c]);
                    #define o(j) *(v4hf*)(targets[c].data+targetBlockPtr+j*stride) = __builtin_shufflevector(halfs, halfs, j*4+0, j*4+1, j*4+2, j*4+3);
                    o(0)o(1)o(2)o(3)
                    #undef o
                }
            }
        }
    } else { // Specific path without Z
        const uint stride = targets[0].stride;
        static constexpr v16si pixelSeq {v8si{(4*4)*0,(4*4)*1,(4*4)*2,(4*4)*3,(4*4)*4,(4*4)*5,(4*4)*6,(4*4)*7},v8si{(4*4)*8,(4*4)*9,(4*4)*10,(4*4)*11,(4*4)*12,(4*4)*13,(4*4)*14,(4*4)*15}};
        for(uint tileY: range(height)) for(uint tileX: range(width)) {
            Tile<C>& tile = tiles[tileY*width+tileX];
            uint const targetTilePtr = tileY*16*stride + tileX*16;
            if(tile.needClear) { // Empty
                for(uint y: range(16)) for(uint x: range(16)) for(uint c: range(C)) targets[c][targetTilePtr+y*stride+x] = clear[c];
                continue;
            }
            for(uint blockY: range(4)) for(uint blockX: range(4)) {
                const uint blockI = blockY*4+blockX;
                const uint blockPtr = blockI*(4*4);
                v16sf pixels[C];
                const mask16 multisample = tile.multisample[blockI];
                if(!multisample) { // No multisampled pixel in block => Directly load block without any multisampled pixels to blend
                    for(uint c: range(C)) pixels[c] = tile.pixels[c][blockI];
                } else {
                    // Resolves (average samples of) multisampled pixels
                    for(uint c: range(C)) {
                        v16sf sum = v16sf(0);
                        for(uint sampleI: range(4*4)) sum += gather((float*)(tile.samples[c]+blockPtr)+sampleI, pixelSeq);
                        const v16sf scale = v16sf(1./(4*4));
                        pixels[c] = blend(tile.pixels[c][blockI], scale*sum, mask(multisample));
                    }
                }
                // Converts to half and untiles block of pixels
                const uint targetBlockPtr = targetTilePtr+blockY*4*stride+blockX*4;
                for(uint c: range(C)) {
                    v16hf halfs = toHalf(pixels[c]);
                    #define o(j) *(v4hf*)(targets[c].data+targetBlockPtr+j*stride) = __builtin_shufflevector(halfs, halfs, j*4+0, j*4+1, j*4+2, j*4+3);
                    o(0)o(1)o(2)o(3)
                    #undef o
                }
            }
        }
    }
}

// 4×4 xy steps constant mask for the 4 possible reject corner
static const v16sf X[4] = {
    {0,1,2,3,
     0,1,2,3,
     0,1,2,3,
     0,1,2,3},
    {0,1,2,3,
     0,1,2,3,
     0,1,2,3,
     0,1,2,3},
    {-3,-2,-1,0,
     -3,-2,-1,0,
     -3,-2,-1,0,
     -3,-2,-1,0,},
    {-3,-2,-1,0,
     -3,-2,-1,0,
     -3,-2,-1,0,
     -3,-2,-1,0,},
};
static const v16sf Y[4] = {
    {0,0,0,0,
     1,1,1,1,
     2,2,2,2,
     3,3,3,3},
    {-3,-3,-3,-3,
     -2,-2,-2,-2,
     -1,-1,-1,-1,
      0, 0, 0, 0},
    {0,0,0,0,
     1,1,1,1,
     2,2,2,2,
     3,3,3,3},
    {-3,-3,-3,-3,
     -2,-2,-2,-2,
     -1,-1,-1,-1,
      0, 0, 0, 0},
};
// 4×4 xy steps from pixel origin to sample center
static const v16sf X0s = ::X[0]+v16sf(1./2);
static const v16sf Y0s = ::Y[0]+v16sf(1./2);

template<class Shader> struct RenderPass {
    // Shading parameters
    typedef typename Shader::FaceAttributes FaceAttributes;
    static constexpr int V = Shader::V;
    const Shader& shader;
     // implements Vec<float, C> Shader::operator()(FaceAttributes, float z, float[V] varyings);
     // implements Vec<v16sf, C> Shader::operator()(FaceAttributes, v16sf z, v16sf[V] varyings, v16si mask);

    // Geometry to rasterize
    struct Face { //~1K/face (streamed)
        v16sf blockRejectStep[3], blockAcceptStep[3], pixelRejectStep[3], pixelAcceptStep[3], sampleStep[3]; // Precomputed step grids
        vec2 edges[3]; // triangle edge equations
        float binReject[3], binAccept[3]; // Initial distance step at a bin reject/accept corner
        vec3 Eiw, Ez; // Linearly interpolated attributes (1/w, z)
        vec3 varyings[V]; // Varying (perspective-interpolated (E/w)) vertex attributes
        FaceAttributes faceAttributes; // Custom constant face attributes
    };
    uint faceCapacity;
    buffer<Face> faces=0;
    uint faceCount=0;

    // Bins to sort faces
    uint width, height; // Tiles
    struct Bin {
        uint16 faceCount = 0;
        uint16 faces[13318];
    };
    buffer<Bin> bins=0;

    // Profiling counters
    profile(int64 rasterTime=0; int64 pixelTime=0; int64 sampleTime=0; int64 sampleFirstTime=0; int64 sampleOverTime=0;)
    uint64 totalTime=0;

    RenderPass(const Shader& shader) : shader(shader) {}
    /// Resets bins and faces for a new setup.
    template<int C> void setup(const RenderTarget<C>& target, uint faceCapacity) {
        assert_(target.size);
        if(width != target.width || height != target.height) {
            width = target.width, height = target.height;
            bins = buffer<Bin>(width*height);
        }
        if(this->faceCapacity != faceCapacity) {
            if(faceCapacity>65536) { error("Too many faces",faceCapacity); return; }
            this->faceCapacity = faceCapacity;
            faces = buffer<Face>(this->faceCapacity);
        }
        bins.clear();
        faceCount=0;
    }

    // Implementation is inline to allow per-pass face attributes specialization and inline shader calls

    /// Submits triangles for binning, actual rendering is deferred until render
    /// \note Device coordinates are not normalized, positions should be in [0..4·Width],[0..4·Height]
    /// \note Accepts CW winding in the left-handed coordinates system (Z-) i.e CCW winding in original right-handed system
    void submit(vec4 A, vec4 B, vec4 C, const vec3 vertexAttributes[V], FaceAttributes faceAttributes) {
        if(faceCount>=faceCapacity) { error("Face overflow"_); return; }
        Face& face = faces[faceCount];
        mat3 M = mat3(vec3(A.xy()/A.w, 1), vec3(B.xy()/B.w, 1), vec3(C.xy()/C.w, 1));
        // E = E.cofactor(); // Edge equations are now columns of E
        // Specialization without multiplications by 1s :
        mat3 E;
        E(0,0) =  (M(1,1) - M(1,2)), E(0,1) = -(M(1,0) - M(1,2)), E(0,2) =  (M(1,0) - M(1,1));
        E(1,0) = -(M(0,1) - M(0,2)), E(1,1) =  (M(0,0) - M(0,2)), E(1,2) = -(M(0,0) - M(0,1));
        E(2,0) =  (M(0,1) * M(1,2) - M(1,1) * M(0,2)), E(2,1) = -(M(0,0) * M(1,2) - M(1,0) * M(0,2)), E(2,2) =  (M(0,0) * M(1,1) - M(0,1) * M(1,0));

        if(E[0].x>0/*dy<0*/ || (E[0].x==0/*dy=0*/ && E[0].y<0/*dx<0*/)) E[0].z++;
        if(E[1].x>0/*dy<0*/ || (E[1].x==0/*dy=0*/ && E[1].y<0/*dx<0*/)) E[1].z++;
        if(E[2].x>0/*dy<0*/ || (E[2].x==0/*dy=0*/ && E[2].y<0/*dx<0*/)) E[2].z++;

        for(int e: range(3)) {
            const vec2& edge = face.edges[e] = E[e].xy();
            { // Reject masks
                const float step0 = (edge.x<0?edge.x:0) + (edge.y<0?edge.y:0);
                // Initial reject corner distance
                face.binReject[e] = E[e].z + 64.f*step0;
                // 4×4 reject corner steps
                const int rejectIndex = (edge.x<0)*2 + (edge.y<0);
                const v16sf step = v16sf(edge.x) * X[rejectIndex] + v16sf(edge.y) * Y[rejectIndex];
                face.blockRejectStep[e] = v16sf(16)*step;
                face.pixelRejectStep[e] = v16sf(4)*step;
                face.sampleStep[e] = -step + v16sf(step0/2); // To center (reversed to allow direct comparison)
            }
            { // Accept masks
                const float step0 = (edge.x>=0?edge.x:0) + (edge.y>=0?edge.y:0);
                // Accept corner distance
                face.binAccept[e] = E[e].z + 64.f*step0;
                // 4×4 accept corner steps
                const int acceptIndex = (edge.x>=0)*2 + (edge.y>=0);
                v16sf step = v16sf(edge.x) * X[acceptIndex] + v16sf(edge.y) * Y[acceptIndex];
                face.blockAcceptStep[e] = v16sf(16)*step;
                face.pixelAcceptStep[e] = v16sf(-4)*step; // Reversed to allow direct comparison
            }
        }

        int2 min = ::max(int2(0,0),int2(floor(::min(::min(A.xy()/A.w,B.xy()/B.w),C.xy()/C.w)))/64);
        int2 max = ::min(int2(width-1,height-1),int2(ceil(::max(::max(A.xy()/A.w,B.xy()/B.w),C.xy()/C.w)))/64);

        for(int binY: range(min.y, max.y+1)) for(int binX: range(min.x, max.x+1)) {
            const vec2 binXY = 64.f*vec2(binX, binY);

            // Trivial reject
            if( face.binReject[0] + dot(face.edges[0], binXY) >= 0 ||
                face.binReject[1] + dot(face.edges[1], binXY) >= 0 ||
                face.binReject[2] + dot(face.edges[2], binXY) >= 0) continue;

            Bin& bin = bins[binY*width+binX];
            if(bin.faceCount>=sizeof(bin.faces)/sizeof(uint16)) { error("Index overflow"); return; }
            bin.faces[bin.faceCount++] = faceCount;
        }

        const float S = E(2,0)+E(2,1)+E(2,2); // Normalization factor (area)
        vec3 iw = vec3(1./A.w, 1./B.w, 1./C.w);
        face.Eiw = E*iw; // No normalization required as factor is eliminated by division (Ev/Eiw)
        face.Ez = E*(vec3(A.z/A.w, B.z/B.w, C.z/C.w)/S); // Normalization required as z is the direct end result
        for(uint i: range(V)) face.varyings[i] = E*(vertexAttributes[i]*iw);
        face.faceAttributes = faceAttributes;

        faceCount++;
    }

    /// Renders one tile
    template<int C> void render(const uint id, Tile<C>& tile, const Bin& bin, const vec2 binXY) {
        // Loops on all faces in the bin
        for(uint16 faceIndex: ref<uint16>(bin.faces, bin.faceCount)) {
            struct DrawBlock { vec2 pos; uint blockIndex; mask16 mask; } blocks[4*4]; uint blockCount=0;
            struct DrawPixel { v16si mask; vec2 pos; uint ptr; } pixels[16*16]; uint pixelCount=0;
            const Face& face = faces[faceIndex];
            {
                profile( int64 start=readCycleCounter(); );
                float binReject[3], binAccept[3];
                for(int e: range(3)) {
                    binReject[e] = face.binReject[e] + dot(face.edges[e], binXY);
                    binAccept[e] = face.binAccept[e] + dot(face.edges[e], binXY);
                }

                // Full bin accept
                if( binAccept[0] < 0 && binAccept[1] < 0 && binAccept[2] < 0 ) {
                    v16sf blockX = v16sf(binXY.x)+v16sf(16)*X[0];
                    v16sf blockY = v16sf(binXY.y)+v16sf(16)*Y[0];
                    for(;blockCount<16;blockCount++)
                        blocks[blockCount] = DrawBlock{vec2(blockX[blockCount], blockY[blockCount]), blockCount, 0xFFFF};
                } else {
                    v16sf blockX = v16sf(binXY.x)+v16sf(16)*X[0];
                    v16sf blockY = v16sf(binXY.y)+v16sf(16)*Y[0];
                    // Loops on 4×4 blocks
                    for(uint blockI: range(4*4)) {
                        float blockReject[3]; for(int e: range(3)) blockReject[e] = binReject[e] + face.blockRejectStep[e][blockI];

                        // Trivial reject
                        if((blockReject[0] >= 0) || (blockReject[1] >= 0) || (blockReject[2] >= 0) ) continue;

                        const vec2 blockXY (blockX[blockI], blockY[blockI]);

                        float blockAccept[3]; for(int e: range(3)) blockAccept[e] = binAccept[e] + face.blockAcceptStep[e][blockI];
                        // Full block accept
                        if(
                                blockAccept[0] < 0 &&
                                blockAccept[1] < 0 &&
                                blockAccept[2] < 0 ) {
                            blocks[blockCount++] = DrawBlock{blockXY, blockI, 0xFFFF};
                            continue;
                        }

                        // 4×4 pixel accept mask
                        uint16 pixelAcceptMask=0xFFFF; // partial block of full pixels
                        for(int e: range(3)) {
                            pixelAcceptMask &= mask(blockAccept[e] < face.pixelAcceptStep[e]);
                        }

                        if(pixelAcceptMask)
                            blocks[blockCount++] = DrawBlock{blockXY, blockI, pixelAcceptMask};
                        // else all pixels were multisampled or rejected

                        // 4×4 pixel reject mask
                        uint16 pixelRejectMask=0; // Partial block of full pixels
                        v16sf pixelReject[3]; // Used to reject samples
                        for(int e: range(3)) {
                            pixelReject[e] = v16sf(blockReject[e]) + face.pixelRejectStep[e];
                            pixelRejectMask |= mask(pixelReject[e] >= 0.f);
                        }

                        // Processes partial pixels
                        uint16 partialPixelMask = ~(pixelRejectMask | pixelAcceptMask);
                        v16sf pixelX = v16sf(blockX[blockI]) + v16sf(4)*X[0];
                        v16sf pixelY = v16sf(blockY[blockI]) + v16sf(4)*Y[0];
                        while(partialPixelMask) {
                            uint pixelI = __builtin_ctz(partialPixelMask);
                            if(pixelI>=16) break;
                            partialPixelMask &= ~(1<<pixelI);

                            // 4×4 samples mask
                            v16si sampleMask =
                                    (v16sf(pixelReject[0][pixelI]) < face.sampleStep[0]) &
                                    (v16sf(pixelReject[1][pixelI]) < face.sampleStep[1]) &
                                    (v16sf(pixelReject[2][pixelI]) < face.sampleStep[2]);
                            const uint16 pixelPtr = blockI*(4*4)+pixelI;
                            if(::mask(sampleMask)) // FIXME
                                pixels[pixelCount++] = DrawPixel{sampleMask, vec2(pixelX[pixelI], pixelY[pixelI]), pixelPtr};
                        }
                    }
                }
                profile( rasterTime += readCycleCounter()-start; )
            }
            {
                profile( int64 start = readCycleCounter(); /*int64 userTime=0;*/ )
                for(const DrawBlock& draw: ref<DrawBlock>(blocks, blockCount)) { // Blocks of fully covered pixels
                    const uint blockIndex = draw.blockIndex;
                    const vec2 blockXY = draw.pos;
                    const v16sf pixelX = v16sf(blockXY.x) + v16sf(4)*X[0];
                    const v16sf pixelY = v16sf(blockXY.y) + v16sf(4)*Y[0];
                    const v16sf XY1x = pixelX+v16sf(4.f/2);
                    const v16sf XY1y = pixelY+v16sf(4.f/2);
                    const v16sf w = 1/( v16sf(face.Eiw.x)*XY1x + v16sf(face.Eiw.y)*XY1y + v16sf(face.Eiw.z)); // Perspective correct interpolation E(w) E(v/w)
                    const v16sf z = v16sf(face.Ez.x)*XY1x + v16sf(face.Ez.y)*XY1y + v16sf(face.Ez.z); // Linear interpolation Ez != E(w) E(z/w)
                    v16sf& Z = tile.pixelZ[blockIndex];
                    const v16si mask = ::mask(draw.mask) & ~::mask(tile.multisample[blockIndex]) & z <= Z & (z >= v16sf(-1));
                    store(Z, mask, z);
                    v16sf centroid[V];
                    for(int i: range(V)) centroid[i] = w*( v16sf(face.varyings[i].x)*XY1x + v16sf(face.varyings[i].y)*XY1y + v16sf(face.varyings[i].z));
                    Vec<v16sf, C> src = shader.template shade(0, face.faceAttributes, z, centroid, mask);
                    for(uint c: range(C)) store(tile.pixels[c][blockIndex], mask, src._[c]);

                    for(uint pixelI: range(4*4)) {
                        if(!(draw.mask&(1<<pixelI))) continue;
                        const uint pixelPtr = blockIndex*(4*4)+pixelI;
                        if(tile.multisample[blockIndex]&(1<<pixelI)) { // Subsample Z-Test
                            // 2D coordinates vector
                            const v16sf sampleX = v16sf(pixelX[pixelI]) + X0s, sampleY = v16sf(pixelY[pixelI]) + Y0s;
                            // Interpolates w for perspective correction
                            const v16sf w = 1/(v16sf(face.Eiw.x)*sampleX + v16sf(face.Eiw.y)*sampleY + v16sf(face.Eiw.z));
                            // Interpolates perspective correct z
                            const v16sf z = v16sf(face.Ez.x)*sampleX + v16sf(face.Ez.y)*sampleY + v16sf(face.Ez.z);

                            // Performs Z-Test
                            v16sf& sampleZ = tile.sampleZ[pixelPtr];
                            const v16si visibleMask = (z <= sampleZ) & (z >= v16sf(-1));
                            const mask16 mask = ::mask(visibleMask);
                            if(!mask) continue; // Avoids NaN centroids (FIXME: HiZ)

                            // Stores accepted pixels in Z buffer
                            store(sampleZ, visibleMask, z);

                            // Counts visible samples
                            float centroid[V];
                            const float visibleSampleCount = __builtin_popcount(mask);

                            // Computes vertex attributes at all samples
                            for(int i: range(V)) {
                                const v16sf samples = w*(v16sf(face.varyings[i].x)*sampleX + v16sf(face.varyings[i].y)*sampleY + v16sf(face.varyings[i].z));
                                // Zeroes hidden samples
                                const v16sf visible = and(visibleMask, samples);
                                // Averages on the visible samples
                                centroid[i] = hsum(visible) / visibleSampleCount;
                            }
                            const float centroidZ = hsum(and(visibleMask, z)) / visibleSampleCount; // FIXME: asserts elimination when shader ignores Z

                            Vec<float, C> src = shader.template shade(id, face.faceAttributes, centroidZ, centroid);
                            for(uint c: range(C)) store(tile.samples[c][pixelPtr], visibleMask, v16sf(src._[c]));
                        }
                    }
                }
                profile( pixelTime += readCycleCounter()-start; )
            }
            {
                profile( int64 start = readCycleCounter(); )
                for(uint i: range(pixelCount)) { // Partially covered pixel of samples
                    const DrawPixel& draw = pixels[i];
                    const uint pixelPtr = draw.ptr;
                    const vec2 pixelXY = draw.pos;

                    // 2D coordinates vector
                    const v16sf sampleX = v16sf(pixelXY.x) + X0s, sampleY = v16sf(pixelXY.y) + Y0s;
                    // Interpolates w for perspective correction
                    const v16sf w = 1/(v16sf(face.Eiw.x)*sampleX + v16sf(face.Eiw.y)*sampleY + v16sf(face.Eiw.z));
                    // Interpolates perspective correct z
                    const v16sf z = v16sf(face.Ez.x)*sampleX + v16sf(face.Ez.y)*sampleY + v16sf(face.Ez.z);

                    // Convert single sample pixel to multisampled pixel
                    if(!(tile.multisample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                        // Performs Z-Test
                        const float pixelZ = ((float*)tile.pixelZ)[pixelPtr];
                        const v16si visibleMask = (z <= pixelZ) & (z >= v16sf(-1)) & draw.mask;
                        const mask16 mask = ::mask(visibleMask);
                        if(!mask) continue; // Avoids NaN centroids (FIXME: HiZ)

                        // Set multisampled pixel flag
                        tile.multisample[pixelPtr/16] |= (1<<(pixelPtr%16));

                        // Blends accepted pixels in multisampled Z buffer
                        tile.sampleZ[pixelPtr] = blend(v16sf(pixelZ), z, visibleMask);

                        // Counts visible samples
                        const float visibleSampleCount = __builtin_popcount(mask);

                        // Computes vertex attributes at all samples
                        float centroid[V];
                        for(int i: range(V)) {
                            const v16sf samples = w*(v16sf(face.varyings[i].x)*sampleX + v16sf(face.varyings[i].y)*sampleY + v16sf(face.varyings[i].z));
                            centroid[i] = hsum(and(visibleMask, samples)) / visibleSampleCount; // Averages on the visible samples
                        }
                        const float centroidZ = hsum(and(visibleMask, z)) / visibleSampleCount; // FIXME: asserts elimination when shader ignores Z
                        Vec<float, C> src = shader.template shade(id, face.faceAttributes, centroidZ, centroid);
                        for(uint c: range(C)) tile.samples[c][pixelPtr] = blend(v16sf(((float*)tile.pixels[c])[pixelPtr]), v16sf(src._[c]), visibleMask);
                    } else {
                        // Performs Z-Test
                        v16sf& sampleZ = tile.sampleZ[pixelPtr];
                        const v16si visibleMask = (z <= sampleZ) & (z >= v16sf(-1)) & draw.mask;
                        const mask16 mask = ::mask(visibleMask);
                        if(!mask) continue; // Avoids NaN centroids (FIXME: HiZ)

                        // Stores accepted pixels in Z buffer
                        store(sampleZ, visibleMask, z);

                        // Counts visible samples
                        const float visibleSampleCount = __builtin_popcount(mask);

                        // Computes vertex attributes at all samples
                        float centroid[V];
                        for(int i: range(V)) {
                            const v16sf samples = w*(v16sf(face.varyings[i].x)*sampleX + v16sf(face.varyings[i].y)*sampleY + v16sf(face.varyings[i].z));
                            centroid[i] = hsum(and(visibleMask, samples)) / visibleSampleCount; // Averages on the visible samples
                        }
                        const float centroidZ = hsum(and(visibleMask, z)) / visibleSampleCount; // FIXME: asserts elimination when shader ignores Z
                        Vec<float, C> src = shader.template shade(id, face.faceAttributes, centroidZ, centroid);
                        for(int c: range(C)) store(tile.samples[c][pixelPtr], visibleMask, v16sf(src._[c]));
                    }
                }
                profile( sampleTime += readCycleCounter()-start; )
            }
        }
    }

    /// Renders all tiles
    // For each bin, rasterizes and shades all triangles
    template<int C> void render(RenderTarget<C>& target) {
        assert_(width == target.width && height == target.height);
        if(!bins || !faces) return;
        //for(uint binIndex: range(width*height)) {
        parallel_for(0, width*height, [this, &target](const uint id, uint binIndex) {
            if(!bins[binIndex].faceCount) return;

            Tile<C>& tile = target.tiles[binIndex];
            if(tile.needClear) {
                mref<uint16>(tile.multisample).clear();
                mref<v16sf>(tile.pixelZ).clear(v16sf(target.clearZ));
                for(int c: range(C)) mref<v16sf>(tile.pixels[c]).clear(v16sf(target.clear[c]));
                tile.needClear = false;
            }

            const vec2 binXY = 64.f*vec2(binIndex%target.width,binIndex/target.width);
            render(id, tile, bins[binIndex], binXY);
        });
    }
};
