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
#include "thread.h"
#include "time.h"
#include "simd.h"
#include <pthread.h> //pthread
#include "image.h"

#define PROFILE
#ifdef PROFILE
#define profile(s) s
#else
#define profile(s)
#endif

/// 64×64 pixels tile for L1 cache locality (64×64×RGBZ×float~64KB)
struct Tile { // 64KB framebuffer (L1)
    v16sf depth[4*4],blue[4*4],green[4*4],red[4*4];
    v16sf subdepth[16*16],subblue[16*16],subgreen[16*16],subred[16*16];
    uint16 subsample[16]; // Per-pixel flag to trigger subpixel operations
    uint8 cleared, lastCleared;
    Tile();
};

/// Tiled render target
struct RenderTarget {
    int2 size = 0; // Pixels
    uint width = 0, height = 0; // Tiles
    buffer<Tile> tiles;
    float depth; bgr3f backgroundColor;

    // Allocates all bins, flags them to be cleared
    void setup(int2 size, float depth=inff, bgr3f backgroundColor=0) {
        if(this->size != size) {
            this->size = size;
            width = align(64,size.x*4)/64;
            height = align(64,size.y*4)/64;
            tiles = buffer<Tile>(width*height);
        }
        this->depth = depth;
        this->backgroundColor = backgroundColor;
        for(Tile& tile: tiles) { tile.lastCleared=1; tile.cleared=0; } // Forces initial background blit to target
    }

    // Resolves internal MSAA linear framebuffer to RGB linear half buffers
    void resolve(const ImageH& B, const ImageH& G, const ImageH& R);
};

void convert(const Image& target, const ImageH& B, const ImageH& G, const ImageH& R);

// 4×4 xy steps constant mask for the 4 possible reject corner
static constexpr vec2 XY[4][4*4] = {
    {
        vec2(0,0),vec2(1,0),vec2(2,0),vec2(3,0),
        vec2(0,1),vec2(1,1),vec2(2,1),vec2(3,1),
        vec2(0,2),vec2(1,2),vec2(2,2),vec2(3,2),
        vec2(0,3),vec2(1,3),vec2(2,3),vec2(3,3),
    },
    {
        vec2(0,-3),vec2(1,-3),vec2(2,-3),vec2(3,-3),
        vec2(0,-2),vec2(1,-2),vec2(2,-2),vec2(3,-2),
        vec2(0,-1),vec2(1,-1),vec2(2,-1),vec2(3,-1),
        vec2(0,-0),vec2(1,-0),vec2(2,-0),vec2(3,-0),
    },
    {
        vec2(-3,0),vec2(-2,0),vec2(-1,0),vec2(-0,0),
        vec2(-3,1),vec2(-2,1),vec2(-1,1),vec2(-0,1),
        vec2(-3,2),vec2(-2,2),vec2(-1,2),vec2(-0,2),
        vec2(-3,3),vec2(-2,3),vec2(-1,3),vec2(-0,3),
    },
    {
        vec2(-3,-3),vec2(-2,-3),vec2(-1,-3),vec2(-0,-3),
        vec2(-3,-2),vec2(-2,-2),vec2(-1,-2),vec2(-0,-2),
        vec2(-3,-1),vec2(-2,-1),vec2(-1,-1),vec2(-0,-1),
        vec2(-3,-0),vec2(-2,-0),vec2(-1,-0),vec2(-0,-0),
    }
};

template<class Shader> struct RenderPass {
    // Shading parameters
    typedef typename Shader::FaceAttributes FaceAttributes;
    static constexpr int V = Shader::V;
    //static constexpr bool blend = Shader::blend;
    const Shader& shader;

    // Geometry to rasterize
    struct Face { //~1K/face (streamed)
        v16sf blockRejectStep[3], blockAcceptStep[3], pixelRejectStep[3], pixelAcceptStep[3], sampleStep[3]; // Precomputed step grids
        vec2 edges[3]; // triangle edge equations
        float binReject[3], binAccept[3]; // Initial distance step at a bin reject/accept corner
        vec3 i1, iz; vec3 varyings[V]; // Varying (perspective-interpolated) vertex attributes
        FaceAttributes faceAttributes; // Custom constant face attributes
    };
    uint faceCapacity;
    buffer<Face> faces=0;
    uint faceCount=0;

    // Bins to sort faces
    uint width, height; // Tiles
    struct Bin {
        uint16 faceCount = 0;
        uint16 faces[7]; //511
    };
    buffer<Bin> bins=0;

    // Profiling counters
    profile(int64 rasterTime=0; int64 pixelTime=0; int64 sampleTime=0; int64 sampleFirstTime=0; int64 sampleOverTime=0;)// int64 userTime=0;)
    uint64 totalTime=0;

    RenderPass(const Shader& shader) : shader(shader) {}
    /// Resets bins and faces for a new setup.
    void setup(const RenderTarget& target, uint faceCapacity) {
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
    void submit(vec3 A, vec3 B, vec3 C, const vec3 vertexAttributes[V], FaceAttributes faceAttributes) {
        if(faceCount>=faceCapacity) { error("Face overflow"_); return; }
        Face& face = faces[faceCount];
        mat3 E = mat3(vec3(A.xy(), 1), vec3(B.xy(), 1), vec3(C.xy(), 1));
        E = E.cofactor(); // Edge equations are now columns of E
        if(E[0].x>0/*dy<0*/ || (E[0].x==0/*dy=0*/ && E[0].y<0/*dx<0*/)) E[0].z--;
        if(E[1].x>0/*dy<0*/ || (E[1].x==0/*dy=0*/ && E[1].y<0/*dx<0*/)) E[1].z--;
        if(E[2].x>0/*dy<0*/ || (E[2].x==0/*dy=0*/ && E[2].y<0/*dx<0*/)) E[2].z--;

        for(int e: range(3)) {
            const vec2& edge = face.edges[e] = E[e].xy();
            { // Reject masks
                float step0 = (edge.x>0?edge.x:0) + (edge.y>0?edge.y:0);
                // Initial reject corner distance
                face.binReject[e] = E[e].z + 64.f*step0;
                // 4×4 reject corner steps
                int rejectIndex = (edge.x>0)*2 + (edge.y>0);
                for(int i: range(4*4)) { // FIXME: SIMD
                    float step = dot(edge, XY[rejectIndex][i]);
                    face.blockRejectStep[e][i] = 16.f*step;
                    face.pixelRejectStep[e][i] = 4.f*step;
                    face.sampleStep[e][i] = -(1.f*step-1.f/2*step0); // To center (reversed to allow direct comparison)
                }
            }

            { // Accept masks
                float step0 = (edge.x<=0?edge.x:0) + (edge.y<=0?edge.y:0);
                // Accept corner distance
                face.binAccept[e] = E[e].z + 64.f*step0;
                // 4×4 accept corner steps
                int acceptIndex = (edge.x<=0)*2 + (edge.y<=0);
                for(int i: range(4*4)) { // FIXME: SIMD
                    float step = dot(edge, XY[acceptIndex][i]);
                    face.blockAcceptStep[e][i] = 16.f*step;
                    face.pixelAcceptStep[e][i] = -4.f*step; // Reversed to allow direct comparison
                }
            }
        }
        face.i1 = E[0]+E[1]+E[2];
        face.iz = E*vec3(A.z,B.z,C.z);
        for(int i=0;i<V;i++) face.varyings[i] = E*vertexAttributes[i];
        face.faceAttributes=faceAttributes;

        int2 min = ::max(int2(0,0),int2(floor(::min(::min(A.xy(),B.xy()),C.xy())))/64);
        int2 max = ::min(int2(width-1,height-1),int2(ceil(::max(::max(A.xy(),B.xy()),C.xy())))/64);

        for(int binY: range(min.y, max.y+1)) for(int binX: range(min.x, max.x+1)) {
            const vec2 binXY = 64.f*vec2(binX, binY);

            // Trivial reject
            if( face.binReject[0] + dot(face.edges[0], binXY) <= 0 ||
                face.binReject[1] + dot(face.edges[1], binXY) <= 0 ||
                face.binReject[2] + dot(face.edges[2], binXY) <= 0) continue;

            Bin& bin = bins[binY*width+binX];
            if(bin.faceCount>=sizeof(bin.faces)/sizeof(uint16)) { error("Index overflow"); return; }
            bin.faces[bin.faceCount++] = faceCount;
        }
        faceCount++;
    }

    /// Renders one tile
    void render(Tile& tile, const Bin& bin, const vec2 binXY) {
        // Loops on all faces in the bin
        for(uint16 faceIndex: ref<uint16>(bin.faces, bin.faceCount)) {
            struct DrawBlock { vec2 pos; uint ptr; uint mask; } blocks[4*4]; uint blockCount=0;
            struct DrawPixel { v16sf mask; vec2 pos; uint ptr; } pixels[16*16]; uint pixelCount=0;
            const Face& face = faces[faceIndex];
            {
                profile( int64 start=readCycleCounter(); );
                float binReject[3], binAccept[3];
                for(int e=0;e<3;e++) {
                    binReject[e] = face.binReject[e] + dot(face.edges[e], binXY);
                    binAccept[e] = face.binAccept[e] + dot(face.edges[e], binXY);
                }

                // Full bin accept
                if( binAccept[0] > 0 && binAccept[1] > 0 && binAccept[2] > 0 ) {
                    for(;blockCount<16;blockCount++)
                        blocks[blockCount] = DrawBlock{binXY+16.f*XY[0][blockCount], blockCount*(4*4), 0xFFFF};
                } else {
                    // Loops on 4×4 blocks
                    for(uint blockI=0; blockI<4*4; blockI++) {
                        float blockReject[3]; for(int e=0;e<3;e++) blockReject[e] = binReject[e] + face.blockRejectStep[e][blockI];

                        // Trivial reject
                        if((blockReject[0] <= 0) || (blockReject[1] <= 0) || (blockReject[2] <= 0) ) continue;

                        const uint16 blockPtr = blockI*(4*4);
                        const vec2 blockXY = binXY+16.f*XY[0][blockI];

                        float blockAccept[3]; for(int e=0;e<3;e++) blockAccept[e] = binAccept[e] + face.blockAcceptStep[e][blockI];
                        // Full block accept
                        if(
                                blockAccept[0] > 0 &&
                                blockAccept[1] > 0 &&
                                blockAccept[2] > 0 ) {
                            blocks[blockCount++] = DrawBlock{blockXY, blockPtr, 0xFFFF};
                            continue;
                        }

                        // 4×4 pixel accept mask
                        uint16 pixelAcceptMask=0xFFFF; // partial block of full pixels
                        for(int e=0;e<3;e++) {
                            pixelAcceptMask &= mask(blockAccept[e] > face.pixelAcceptStep[e]);
                        }

                        if(pixelAcceptMask)
                            blocks[blockCount++] = DrawBlock{blockXY, blockPtr, pixelAcceptMask};
                        // else all pixels were subsampled or rejected

                        // 4×4 pixel reject mask
                        uint16 pixelRejectMask=0; // partial block of full pixels
                        v16sf pixelReject[3]; //used to reject samples
                        for(int e=0;e<3;e++) {
                            pixelReject[e] = blockReject[e] + face.pixelRejectStep[e];
                            pixelRejectMask |= mask(pixelReject[e] <= 0);
                        }

                        // Processes partial pixels
                        uint16 partialPixelMask = ~(pixelRejectMask | pixelAcceptMask);
                        while(partialPixelMask) {
                            uint pixelI = __builtin_ctz(partialPixelMask);
                            if(pixelI>=16) break;
                            partialPixelMask &= ~(1<<pixelI);

                            // 4×4 samples mask
                            v16sf sampleMask =
                                    (v16sf(pixelReject[0][pixelI]) > face.sampleStep[0]) &
                                    (v16sf(pixelReject[1][pixelI]) > face.sampleStep[1]) &
                                    (v16sf(pixelReject[2][pixelI]) > face.sampleStep[2]);
                            const vec2 pixelXY = 4.f*XY[0][pixelI];
                            const uint16 pixelPtr = blockPtr+pixelI;
                            pixels[pixelCount++] = DrawPixel{sampleMask, blockXY+pixelXY, pixelPtr};
                        }
                    }
                }
                profile( rasterTime += readCycleCounter()-start; )
            }
            // 4×4 xy steps from pixel origin to sample center
            static const v16sf X = v16sf(0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3)+1./2;
            static const v16sf Y = v16sf(0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3)+1./2;
            {
                profile( int64 start = readCycleCounter(); /*int64 userTime=0;*/ )
                for(uint i=0; i<blockCount; i++) { // Blocks of fully covered pixels
                    const DrawBlock& draw = blocks[i];
                    const uint blockPtr = draw.ptr;
                    const vec2 blockXY = draw.pos;
                    const uint16 mask = draw.mask;
                    for(uint pixelI=0; pixelI<4*4; pixelI++) {
                        if(!(mask&(1<<pixelI))) continue;
                        const uint pixelPtr = blockPtr+pixelI;
                        const vec2 pixelXY = blockXY+4.f*XY[0][pixelI];

                        if(!(tile.subsample[blockPtr]&(1<<pixelI))) { // Pixel coverage on single sample pixel
                            vec3 XY1 = vec3(pixelXY+vec2(4.f/2, 4.f/2), 1.f);
                            float w = 1/dot(face.i1,XY1);
                            float z = w*dot(face.iz,XY1);

                            float& depth = ((float*)tile.depth)[pixelPtr];
                            if(z > depth) continue;
                            depth = z;

                            float centroid[V]; for(int i=0;i<V;i++) centroid[i]=w*dot(face.varyings[i],XY1);
                            //profile( int64 start = readCycleCounter(); );
                            bgra4f src = shader(face.faceAttributes,centroid);
                            //profile( userTime += readCycleCounter()-start; );
                            float& dstB = ((float*)tile.blue)[pixelPtr];
                            float& dstG = ((float*)tile.green)[pixelPtr];
                            float& dstR = ((float*)tile.red)[pixelPtr];
                            if(Shader::blend) {
                                dstB=(1-src.a)*dstB+src.a*src.b;
                                dstG=(1-src.a)*dstG+src.a*src.g;
                                dstR=(1-src.a)*dstR+src.a*src.r;
                            } else {
                                dstB=src.b;
                                dstG=src.g;
                                dstR=src.r;
                            }
                        } else { // Subsample Z-Test
                            // 2D coordinates vector
                            const v16sf sampleX = pixelXY.x + X, sampleY = pixelXY.y + Y;
                            // Interpolates w for perspective correction
                            const v16sf w = 1/(v16sf(face.i1.x)*sampleX + v16sf(face.i1.y)*sampleY + v16sf(face.i1.z));
                            // Interpolates perspective correct z
                            const v16sf z = w*(v16sf(face.iz.x)*sampleX + v16sf(face.iz.y)*sampleY + v16sf(face.iz.z));

                            // Performs Z-Test
                            v16sf& subpixel = tile.subdepth[pixelPtr];
                            const v16si visibleMask = (z >= subpixel);

                            // Stores accepted pixels in Z buffer
                            maskstore(subpixel, visibleMask, z);

                            // Counts visible samples
                            float centroid[V];
                            float visibleSampleCount = __builtin_popcount(::mask(visibleMask));

                            // Computes vertex attributes at all samples
                            for(int i=0;i<V;i++) {
                                const v16sf samples = w*(v16sf(face.varyings[i].x)*sampleX + v16sf(face.varyings[i].y)*sampleY + v16sf(face.varyings[i].z));
                                // Zeroes hidden samples
                                const v16sf visible = samples & visibleMask;
                                // Averages on the visible samples
                                centroid[i] = sum16(visible) / visibleSampleCount;
                            }

                            //profile( int64 start = readCycleCounter() );
                            bgra4f src = shader(face.faceAttributes,centroid);
                            //profile( userTime += readCycleCounter()-start );
                            v16sf& dstB = tile.subblue[pixelPtr];
                            v16sf& dstG = tile.subgreen[pixelPtr];
                            v16sf& dstR = tile.subred[pixelPtr];
                            if(Shader::blend) {
                                maskstore(dstB, visibleMask, v16sf(1-src.a)*dstB+v16sf(src.a*src.b));
                                maskstore(dstG, visibleMask, v16sf(1-src.a)*dstG+v16sf(src.a*src.g));
                                maskstore(dstR, visibleMask, v16sf(1-src.a)*dstR+v16sf(src.a*src.r));
                            } else {
                                maskstore(dstB, visibleMask, src.b);
                                maskstore(dstG, visibleMask, src.g);
                                maskstore(dstR, visibleMask, src.r);
                            }
                        }
                    }
                }
                profile( /*this->userTime+=userTime;*/ pixelTime += readCycleCounter()-start;)// - userTime; )
            }
            {
                profile( int64 start = readCycleCounter(); /*int64 userTime=0;*/ )
                for(uint i: range(pixelCount)) { // Partially covered pixel of samples
                    const DrawPixel& draw = pixels[i];
                    const uint pixelPtr = draw.ptr;
                    const vec2 pixelXY = draw.pos;

                    // 2D coordinates vector
                    const v16sf sampleX = pixelXY.x + X, sampleY = pixelXY.y + Y;
                    // Interpolates w for perspective correction
                    const v16sf w = 1/(v16sf(face.i1.x)*sampleX + v16sf(face.i1.y)*sampleY + v16sf(face.i1.z));
                    // Interpolates perspective correct z
                    const v16sf z = w*(v16sf(face.iz.x)*sampleX + v16sf(face.iz.y)*sampleY + v16sf(face.iz.z));

                    // Convert single sample pixel to subsampled pixel
                    if(!(tile.subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                        //profile( int64 start = readCycleCounter() );

                        // Set subsampled pixel flag
                        tile.subsample[pixelPtr/16] |= (1<<(pixelPtr%16));

                        // Performs Z-Test
                        float pixelZ = ((float*)tile.depth)[pixelPtr];
                        const v16si visibleMask = (z <= pixelZ) & draw.mask;

                        // Blends accepted pixels in subsampled Z buffer
                        tile.subdepth[pixelPtr] = blend(v16sf(pixelZ), z, visibleMask);

                        // Counts visible samples
                        float visibleSampleCount = __builtin_popcount(::mask(visibleMask));

                        // Computes vertex attributes at all samples
                        float centroid[V];
                        for(int i=0;i<V;i++) {
                            const v16sf samples = w*(v16sf(face.varyings[i].x)*sampleX + v16sf(face.varyings[i].y)*sampleY + v16sf(face.varyings[i].z));
                            // Zeroes hidden samples
                            const v16sf visible = samples & visibleMask;
                            // Averages on the visible samples
                            centroid[i] = sum16(visible) / visibleSampleCount;
                        }

                        //profile( int64 userStart = readCycleCounter() );
                        bgra4f src = shader(face.faceAttributes,centroid);
                        //profile( int64 userEnd = readCycleCounter(); userTime += userEnd-userStart );
                        v16sf& dstB = tile.subblue[pixelPtr];
                        v16sf& dstG = tile.subgreen[pixelPtr];
                        v16sf& dstR = tile.subred[pixelPtr];
                        float pixelB = ((float*)tile.blue)[pixelPtr];
                        float pixelG = ((float*)tile.green)[pixelPtr];
                        float pixelR = ((float*)tile.red)[pixelPtr];
                        if(Shader::blend) {
                            dstB = blend(pixelB, (1-src.a)*pixelB+src.a*src.b, visibleMask);
                            dstG = blend(pixelG, (1-src.a)*pixelG+src.a*src.g, visibleMask);
                            dstR = blend(pixelR, (1-src.a)*pixelR+src.a*src.r, visibleMask);
                        } else {
                            dstB = blend(pixelB, src.b, visibleMask);
                            dstG = blend(pixelG, src.g, visibleMask);
                            dstR = blend(pixelR, src.r, visibleMask);
                        }
                        //profile( sampleFirstTime += (userStart-start) + (readCycleCounter()-userEnd); )
                    } else {
                        //profile( int64 start = readCycleCounter() );

                        // Performs Z-Test
                        v16sf& subpixel = tile.subdepth[pixelPtr];
                        const v16si visibleMask = (z <= subpixel) & draw.mask;

                        // Stores accepted pixels in Z buffer
                        maskstore(subpixel, visibleMask, z);

                        // Counts visible samples
                        float visibleSampleCount = __builtin_popcount(::mask(visibleMask));

                        // Computes vertex attributes at all samples
                        float centroid[V];
                        for(int i=0;i<V;i++) {
                            const v16sf samples = w*(v16sf(face.varyings[i].x)*sampleX + v16sf(face.varyings[i].y)*sampleY + v16sf(face.varyings[i].z));
                            // Zeroes hidden samples
                            const v16sf visible = samples & visibleMask;
                            // Averages on the visible samples
                            centroid[i] = sum16(visible) / visibleSampleCount;
                        }

                        //profile( int64 userStart = readCycleCounter() );
                        bgra4f src = shader(face.faceAttributes, centroid);
                        //profile( int64 userEnd = readCycleCounter(); userTime += userEnd-userStart );
                        v16sf& dstB = tile.subblue[pixelPtr];
                        v16sf& dstG = tile.subgreen[pixelPtr];
                        v16sf& dstR = tile.subred[pixelPtr];
                        if(Shader::blend) {
                            maskstore(dstB, visibleMask, v16sf(1-src.a)*dstB+v16sf(src.a*src.b));
                            maskstore(dstG, visibleMask, v16sf(1-src.a)*dstG+v16sf(src.a*src.g));
                            maskstore(dstR, visibleMask, v16sf(1-src.a)*dstR+v16sf(src.a*src.r));
                        } else {
                            maskstore(dstB, visibleMask, src.b);
                            maskstore(dstG, visibleMask, src.g);
                            maskstore(dstR, visibleMask, src.r);
                        }
                        //profile( sampleOverTime += (userStart-start) + (readCycleCounter()-userEnd); )
                    }
                }
                profile( /*this->userTime += userTime;*/ sampleTime += readCycleCounter()-start /*-userTime*/; )
            }
        }
    }

    /// Renders all tiles
    RenderTarget* target;
    uint nextBin;
    static void* start_routine(void* this_) { ((RenderPass*)this_)->run(); return 0; }
    // For each bin, rasterizes and shades all triangles
    void render(RenderTarget& target) {
        if(!bins || !faces) return;
        this->target = &target;
        // Reset counters
        nextBin=0;
        //profile(({rasterTime=0, pixelTime=0, sampleTime=0, sampleFirstTime=0, sampleOverTime=0, userTime=0, totalTime=0;}));
#ifdef PROFILE
        run();
#else
        // Schedules all cores to process tiles
        const int N=8;
        pthread_t threads[N-1];
        for(int i=0;i<N-1;i++) pthread_create(&threads[i],0,start_routine,this);
        run();
        for(int i=0;i<N-1;i++) { void* status; pthread_join(threads[i],&status); }
#endif
    }
    void run() {
        profile( int64 start = readCycleCounter(); );
        // Loops on all bins (64x64 samples (16x16 pixels)) then on passes (improves framebuffer access, passes have less locality)
        for(;;) {
            uint binI = __sync_fetch_and_add(&nextBin,1);
            if(binI>=width*height) break;
            if(!bins[binI].faceCount) continue;

            Tile& tile = target->tiles[binI];
            if(!tile.cleared) {
                mref<uint16>(tile.subsample).clear();
                mref<v16sf>(tile.depth).clear(v16sf(target->depth));
                mref<v16sf>(tile.blue).clear(v16sf(target->backgroundColor.b));
                mref<v16sf>(tile.green).clear(v16sf(target->backgroundColor.g));
                mref<v16sf>(tile.red).clear(v16sf(target->backgroundColor.r));
                tile.cleared=1;
            }

            const vec2 binXY = 64.f*vec2(binI%target->width,binI/target->width);
            render(tile, bins[binI], binXY);
        }
        profile( totalTime += readCycleCounter()-start; );
    }
};