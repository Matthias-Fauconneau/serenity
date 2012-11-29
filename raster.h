/** \file raster.h 3D rasterizer
This rasterizer is an AVX implementation of a tile-based deferred renderer (cf http://www.drdobbs.com/parallel/rasterization-on-larrabee/217200602) :
Triangles are not immediatly rendered but first sorted in 16x16 pixels (64x64 samples) bins.
When all triangles have been setup, each tile is separately rendered.
Tiles can be processed in parallel (I'm using 4 hyperthreaded cores) and only access their local framebuffer (which should fit L1).
As presented in Abrash's article, rasterization is done recursively (4x4 blocks of 4x4 pixels of 4x4 samples) using precomputed step grids.
This architecture allows the rasterizer to leverage 16-wide vector units (on Ivy Bride's 8-wide units, all operations have to be duplicated).
For each face, the rasterizer outputs pixel masks for each blocks (or sample masks for partial pixels).
Then, pixels are depth-tested, shaded and blended in the local framebuffer.
Finally, after all passes have been rendered, the tiles are resolved and copied to the application window buffer.
*/
#pragma once
#include "matrix.h"
#include "process.h"
#include "time.h"
#include "vec16.h"

#define PROFILE
#ifdef PROFILE
#define profile(s) s
#else
#define profile(s)
#endif

/// 64×64 pixels tile for L1 cache locality (64×64×RGBZ×float~64KB)
struct Tile { //64KB framebuffer (L1)
    vec16 depth[4*4],blue[4*4],green[4*4],red[4*4];
    vec16 subdepth[16*16],subblue[16*16],subgreen[16*16],subred[16*16];
    uint16 subsample[16]; //Per-pixel flag to trigger subpixel operations
    uint8 cleared=0, lastCleared=0;
};

/// Tiled render target
struct RenderTarget {
    int2 size = 0; //in pixels
    uint width = 0, height = 0; //in tiles
    Tile* tiles = 0;
    float depth, blue, green, red;

    // Allocates all bins, flags them to be cleared
    void setup(int2 size, float depth=-0x1p16f, float blue=1, float green=1, float red=1) {
        if(this->size != size) {
            if(tiles) unallocate(tiles,width*height);
            width = align(64,size.x*4)/64;
            height = align(64,size.y*4)/64;
            tiles = allocate64<Tile>(width*height);
            for(int i: range(this->width*this->height)) { Tile& tile = tiles[i]; tile.cleared=1; } //force initial background blit
        }
        this->depth=depth;
        this->blue=blue;
        this->green=green;
        this->red=red;
        for(int i: range(this->width*this->height)) { Tile& tile = tiles[i]; tile.lastCleared=tile.cleared; tile.cleared=0; }
    }
    ~RenderTarget() { unallocate(tiles,width*height); }

    // Resolves internal MSAA linear framebuffer for sRGB display on the active X window
    void resolve(int2 position, int2 size);
};

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
    static constexpr bool blend = Shader::blend;
    const Shader& shader;

    // Geometry to rasterize
    struct Face { //~1K/face (streamed)
        vec16 blockRejectStep[3], blockAcceptStep[3], pixelRejectStep[3], pixelAcceptStep[3], sampleStep[3]; // Precomputed step grids
        vec2 edges[3]; // triangle edge equations
        float binReject[3], binAccept[3]; // Initial distance step at a bin reject/accept corner
        vec3 iw, iz; vec3 varyings[V]; //varying (perspective-interpolated) vertex attributes
        FaceAttributes faceAttributes; //custom constant face attributes
    };
    uint faceCapacity;
    Face* faces=0;
    uint faceCount=0;

    // Bins to sort faces
    uint width, height;
    struct Bin {
        uint16 faceCount=0;
        uint16 faces[511];
    };
    Bin* bins=0;

    // Profiling counters
    profile(int64 rasterTime=0; int64 pixelTime=0; int64 sampleTime=0; int64 sampleFirstTime=0; int64 sampleOverTime=0; int64 userTime=0;)
    uint64 totalTime=0;
    // Debug information
    string name;

    RenderPass(const Shader& shader, const ref<byte>& name=""_) : shader(shader), name(name) {}
    /// Resets bins and faces for a new setup.
    void setup(const RenderTarget& target, uint faceCapacity) {
        if(width != target.width || height != target.height) {
            if(bins) unallocate(bins,width*height);
            width=target.width, height=target.height;
            bins = allocate64<Bin>(width*height);
        }
        if(this->faceCapacity != faceCapacity) {
            if(faceCapacity>65536) { error("Too many faces",faceCapacity); return; }
            if(faces) unallocate(faces,this->faceCapacity);
            this->faceCapacity = faceCapacity;
            faces = allocate64<Face>(this->faceCapacity);
        }
        for(uint bin: range(width*height)) { bins[bin].faceCount=0; }
        faceCount=0;
    }
    ~RenderPass(){ if(bins) unallocate(bins,width*height); if(faces) unallocate(faces,faceCapacity); }

    // Implementation is inline to allow per-pass face attributes specialization and inline shader calls

    /// Submits triangles for binning, actual rendering is deferred until render
    /// \note Device coordinates are not normalized, positions should be in [0..4×Width],[0..4×Height]
    void submit(vec4 A, vec4 B, vec4 C, const vec3 vertexAttributes[V], FaceAttributes faceAttributes) {
        if(faceCount>=faceCapacity) { userError(name,"Face overflow"_); return; }
        Face& face = faces[faceCount];
        //assert(abs(A.w-1)<0.01,A.w); assert(abs(B.w-1)<0.01,B.w); assert(abs(C.w-1)<0.01,C.w);
        mat3 E = mat3(A.xyw(), B.xyw(), C.xyw());
        //float det = E.det(); if(det<1) return; //cull backward faces. Let user cull to avoid some setup
        E = E.cofactor(); //edge equations are now columns of E
        if(E[0].x>0/*dy<0*/ || (E[0].x==0/*dy=0*/ && E[0].y<0/*dx<0*/)) E[0].z++;
        if(E[1].x>0/*dy<0*/ || (E[1].x==0/*dy=0*/ && E[1].y<0/*dx<0*/)) E[1].z++;
        if(E[2].x>0/*dy<0*/ || (E[2].x==0/*dy=0*/ && E[2].y<0/*dx<0*/)) E[2].z++;

        for(int e=0;e<3;e++) {
            const vec2& edge = face.edges[e] = E[e].xy();
            { //reject
                float step0 = (edge.x>0?edge.x:0) + (edge.y>0?edge.y:0);
                // initial reject corner distance
                face.binReject[e] = E[e].z + 64.f*step0;
                // 4×4 reject corner steps
                int rejectIndex = (edge.x>0)*2 + (edge.y>0);
                for(int i=0;i<4*4;i++) {
                    float step = dot(edge, XY[rejectIndex][i]);
                    face.blockRejectStep[e][i] = 16.f*step;
                    face.pixelRejectStep[e][i] = 4.f*step;
                    face.sampleStep[e][i] = -(1.f*step-1.f/2*step0); //to center (reversed to allow direct comparison)
                }
            }

            { //accept
                float step0 = (edge.x<=0?edge.x:0) + (edge.y<=0?edge.y:0);
                // accept corner distance
                face.binAccept[e] = E[e].z + 64.f*step0;
                // 4×4 accept corner steps
                int acceptIndex = (edge.x<=0)*2 + (edge.y<=0);
                for(int i=0;i<4*4;i++) {
                    float step = dot(edge, XY[acceptIndex][i]);
                    face.blockAcceptStep[e][i] = 16.f*step;
                    face.pixelAcceptStep[e][i] = -4.f*step; //reversed to allow direct comparison
                }
            }
        }
        face.iw = E[0]+E[1]+E[2];
        face.iz = E*vec3(A.z,B.z,C.z);
        for(int i=0;i<V;i++) face.varyings[i] = E*vertexAttributes[i];
        face.faceAttributes=faceAttributes;

        int2 min = ::max(int2(0,0),int2(floor(::min(::min(A.xy(),B.xy()),C.xy())))/64);
        int2 max = ::min(int2(width-1,height-1),int2(ceil(::max(::max(A.xy(),B.xy()),C.xy())))/64);

        for(int binY=min.y; binY<=max.y; binY++) for(int binX=min.x; binX<=max.x; binX++) {
            const vec2 binXY = 64.f*vec2(binX, binY);

            // trivial reject
            if(
                    face.binReject[0] + dot(face.edges[0], binXY) <= 0 ||
                    face.binReject[1] + dot(face.edges[1], binXY) <= 0 ||
                    face.binReject[2] + dot(face.edges[2], binXY) <= 0) continue;

            Bin& bin = bins[binY*width+binX];
            if(bin.faceCount>=sizeof(bin.faces)/sizeof(uint16)) { userError(name,"Index overflow"); return; }
            bin.faces[bin.faceCount++]=faceCount;
        }
        faceCount++;
    }

    /// Renders one tile
    void render(Tile& tile, const Bin& bin, const vec2 binXY) {
        // Loop on all faces in the bin
        for(uint faceI=0; faceI<bin.faceCount; faceI++) {
            struct DrawBlock { vec2 pos; uint ptr; uint mask; } blocks[4*4]; uint blockCount=0;
            struct DrawPixel { vec16 mask; vec2 pos; uint ptr; } pixels[16*16]; uint pixelCount=0;
            const Face& face = faces[bin.faces[faceI]];
            {
                profile( int64 start=rdtsc(); );
                float binReject[3], binAccept[3];
                for(int e=0;e<3;e++) {
                    binReject[e] = face.binReject[e] + dot(face.edges[e], binXY);
                    binAccept[e] = face.binAccept[e] + dot(face.edges[e], binXY);
                }

                // Full bin accept
                if(
                        binAccept[0] > 0 &&
                        binAccept[1] > 0 &&
                        binAccept[2] > 0 ) {
                    for(;blockCount<16;blockCount++)
                        blocks[blockCount] = DrawBlock __(binXY+16.f*XY[0][blockCount], blockCount*(4*4), 0xFFFF);
                } else {
                    // Loop on 4×4 blocks
                    for(uint blockI=0; blockI<4*4; blockI++) {
                        float blockReject[3]; for(int e=0;e<3;e++) blockReject[e] = binReject[e] + face.blockRejectStep[e][blockI];

                        // trivial reject
                        if((blockReject[0] <= 0) || (blockReject[1] <= 0) || (blockReject[2] <= 0) ) continue;

                        const uint16 blockPtr = blockI*(4*4);
                        const vec2 blockXY = binXY+16.f*XY[0][blockI];

                        float blockAccept[3]; for(int e=0;e<3;e++) blockAccept[e] = binAccept[e] + face.blockAcceptStep[e][blockI];
                        // Full block accept
                        if(
                                blockAccept[0] > 0 &&
                                blockAccept[1] > 0 &&
                                blockAccept[2] > 0 ) {
                            blocks[blockCount++] = DrawBlock __(blockXY, blockPtr, 0xFFFF);
                            continue;
                        }

                        // 4×4 pixel accept mask
                        uint16 pixelAcceptMask=0xFFFF; // partial block of full pixels
                        for(int e=0;e<3;e++) {
                            pixelAcceptMask &= mask(blockAccept[e] > face.pixelAcceptStep[e]);
                        }

                        if(pixelAcceptMask)
                            blocks[blockCount++] = DrawBlock __(blockXY, blockPtr, pixelAcceptMask);
                        //else all pixels were subsampled or rejected

                        // 4×4 pixel reject mask
                        uint16 pixelRejectMask=0; // partial block of full pixels
                        vec16 pixelReject[3]; //used to reject samples
                        for(int e=0;e<3;e++) {
                            pixelReject[e] = blockReject[e] + face.pixelRejectStep[e];
                            pixelRejectMask |= mask(pixelReject[e] <= 0);
                        }

                        // Process partial pixels
                        uint16 partialPixelMask = ~(pixelRejectMask | pixelAcceptMask);
                        while(partialPixelMask) {
                            uint pixelI = __builtin_ctz(partialPixelMask);
                            if(pixelI>=16) break;
                            partialPixelMask &= ~(1<<pixelI);

                            // 4×4 samples mask
                            vec16 sampleMask =
                                    (pixelReject[0][pixelI] > face.sampleStep[0]) &
                                    (pixelReject[1][pixelI] > face.sampleStep[1]) &
                                    (pixelReject[2][pixelI] > face.sampleStep[2]);
                            const vec2 pixelXY = 4.f*XY[0][pixelI];
                            const uint16 pixelPtr = blockPtr+pixelI;
                            pixels[pixelCount++] = DrawPixel __(sampleMask, blockXY+pixelXY, pixelPtr);
                        }
                    }
                }
                profile( rasterTime += rdtsc()-start; )
            }
            // 4×4 xy steps from pixel origin to sample center
            static const vec16 X = vec16(0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3)+1./2;
            static const vec16 Y = vec16(0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3)+1./2;
            {
                profile( int64 start = rdtsc(); int64 userTime=0; )
                for(uint i=0; i<blockCount; i++) { //Blocks of fully covered pixels
                    const DrawBlock& draw = blocks[i];
                    const uint blockPtr = draw.ptr;
                    const vec2 blockXY = draw.pos;
                    const uint16 mask = draw.mask;
                    for(uint pixelI=0; pixelI<4*4; pixelI++) {
                        if(!(mask&(1<<pixelI))) continue;
                        const uint pixelPtr = blockPtr+pixelI;
                        const vec2 pixelXY = blockXY+4.f*XY[0][pixelI];

                        if(!(tile.subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) { // Pixel coverage on single sample pixel
                            vec3 XY1 = vec3(pixelXY+vec2(4.f/2, 4.f/2), 1.f);
                            float w = 1/dot(face.iw,XY1);
                            float z = w*dot(face.iz,XY1);

                            float& depth = tile.depth[pixelPtr/16][pixelPtr%16];
                            if(z < depth) continue;
                            depth = z;

                            float centroid[V]; for(int i=0;i<V;i++) centroid[i]=w*dot(face.varyings[i],XY1);
                            profile( int64 start = rdtsc(); )
                                    vec4 bgra = shader(face.faceAttributes,centroid);
                            profile( userTime += rdtsc()-start; )
                                    float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA=bgra.w;
                            float& dstB = tile.blue[pixelPtr/16][pixelPtr%16];
                            float& dstG = tile.green[pixelPtr/16][pixelPtr%16];
                            float& dstR = tile.red[pixelPtr/16][pixelPtr%16];
                            if(blend) {
                                dstB=(1-srcA)*dstB+srcA*srcB;
                                dstG=(1-srcA)*dstG+srcA*srcG;
                                dstR=(1-srcA)*dstR+srcA*srcR;
                            } else {
                                dstB=srcB;
                                dstG=srcG;
                                dstR=srcR;
                            }
                        } else { // Subsample Z-Test
                            // 2D coordinates vector
                            const vec16 sampleX = pixelXY.x + X, sampleY = pixelXY.y + Y;
                            // Interpolates w for perspective correction
                            const vec16 w = 1/(face.iw.x*sampleX + face.iw.y*sampleY + face.iw.z);
                            // Interpolates perspective correct z
                            const vec16 z = w*(face.iz.x*sampleX + face.iz.y*sampleY + face.iz.z);

                            // Performs Z-Test
                            vec16& subpixel = tile.subdepth[pixelPtr];
                            const vec16 visibleMask = (z >= subpixel);

                            // Stores accepted pixels in Z buffer
                            maskstore(subpixel, visibleMask, z);

                            // Counts visible samples
                            float centroid[V];
                            float visibleSampleCount = __builtin_popcount(::mask(visibleMask));

                            // Computes vertex attributes at all samples
                            for(int i=0;i<V;i++) {
                                const vec16 samples = w*(face.varyings[i].x*sampleX + face.varyings[i].y*sampleY + face.varyings[i].z);
                                // Zeroes hidden samples
                                const vec16 visible = samples & visibleMask;
                                // Averages on the visible samples
                                centroid[i] = sum16(visible) / visibleSampleCount;
                            }

                            profile( int64 start = rdtsc(); )
                                    vec4 bgra = shader(face.faceAttributes,centroid);
                            profile( userTime += rdtsc()-start; )
                                    float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                            vec16& dstB = tile.subblue[pixelPtr];
                            vec16& dstG = tile.subgreen[pixelPtr];
                            vec16& dstR = tile.subred[pixelPtr];
                            if(blend) {
                                maskstore(dstB, visibleMask, (1-srcA)*dstB+srcA*srcB);
                                maskstore(dstG, visibleMask, (1-srcA)*dstG+srcA*srcG);
                                maskstore(dstR, visibleMask, (1-srcA)*dstR+srcA*srcR);
                            } else {
                                maskstore(dstB, visibleMask, srcB);
                                maskstore(dstG, visibleMask, srcG);
                                maskstore(dstR, visibleMask, srcR);
                            }
                        }
                    }
                }
                profile( this->userTime+=userTime; pixelTime += rdtsc()-start - userTime; )
            }
            {
                profile( int64 start = rdtsc(); int64 userTime=0; )
                for(uint i=0; i<pixelCount; i++) { // Partially covered pixel of samples
                    const DrawPixel& draw = pixels[i];
                    const uint pixelPtr = draw.ptr;
                    const vec2 pixelXY = draw.pos;

                    // 2D coordinates vector
                    const vec16 sampleX = pixelXY.x + X, sampleY = pixelXY.y + Y;
                    // Interpolates w for perspective correction
                    const vec16 w = 1/(face.iw.x*sampleX + face.iw.y*sampleY + face.iw.z);
                    // Interpolates perspective correct z
                    const vec16 z = w*(face.iz.x*sampleX + face.iz.y*sampleY + face.iz.z);

                    // Convert single sample pixel to subsampled pixel
                    if(!(tile.subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                        profile( int64 start = rdtsc(); )

                                // Set subsampled pixel flag
                                tile.subsample[pixelPtr/16] |= (1<<(pixelPtr%16));

                        // Performs Z-Test
                        float pixelZ = tile.depth[pixelPtr/16][pixelPtr%16];
                        const vec16 visibleMask =  (z >= pixelZ) & draw.mask;

                        // Blends accepted pixels in subsampled Z buffer
                        tile.subdepth[pixelPtr] = blend16(pixelZ, z, visibleMask);

                        // Counts visible samples
                        float visibleSampleCount = __builtin_popcount(::mask(visibleMask));

                        // Computes vertex attributes at all samples
                        float centroid[V];
                        for(int i=0;i<V;i++) {
                            const vec16 samples = w*(face.varyings[i].x*sampleX + face.varyings[i].y*sampleY + face.varyings[i].z);
                            // Zeroes hidden samples
                            const vec16 visible = samples & visibleMask;
                            // Averages on the visible samples
                            centroid[i] = sum16(visible) / visibleSampleCount;
                        }

                        profile( int64 userStart = rdtsc(); )
                                vec4 bgra = shader(face.faceAttributes,centroid);
                        profile( int64 userEnd = rdtsc(); userTime += userEnd-userStart; )
                        float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                        vec16& dstB = tile.subblue[pixelPtr];
                        vec16& dstG = tile.subgreen[pixelPtr];
                        vec16& dstR = tile.subred[pixelPtr];
                        float pixelB = tile.blue[pixelPtr/16][pixelPtr%16];
                        float pixelG = tile.green[pixelPtr/16][pixelPtr%16];
                        float pixelR = tile.red[pixelPtr/16][pixelPtr%16];
                        if(blend) {
                            dstB = blend16(pixelB, (1-srcA)*pixelB+srcA*srcB, visibleMask);
                            dstG = blend16(pixelG, (1-srcA)*pixelG+srcA*srcG, visibleMask);
                            dstR = blend16(pixelR, (1-srcA)*pixelR+srcA*srcR, visibleMask);
                        } else {
                            dstB = blend16(pixelB, srcB, visibleMask);
                            dstG = blend16(pixelG, srcG, visibleMask);
                            dstR = blend16(pixelR, srcR, visibleMask);
                        }
                        profile( sampleFirstTime += (userStart-start) + (rdtsc()-userEnd); )
                    } else {
                        profile( int64 start = rdtsc(); )

                                // Performs Z-Test
                                vec16& subpixel = tile.subdepth[pixelPtr];
                        const vec16 visibleMask =  (z >= subpixel) & draw.mask;

                        // Stores accepted pixels in Z buffer
                        maskstore(subpixel, visibleMask, z);

                        // Counts visible samples
                        float visibleSampleCount = __builtin_popcount(::mask(visibleMask));

                        // Computes vertex attributes at all samples
                        float centroid[V];
                        for(int i=0;i<V;i++) {
                            const vec16 samples = w*(face.varyings[i].x*sampleX + face.varyings[i].y*sampleY + face.varyings[i].z);
                            // Zeroes hidden samples
                            const vec16 visible = samples & visibleMask;
                            // Averages on the visible samples
                            centroid[i] = sum16(visible) / visibleSampleCount;
                        }

                        profile( int64 userStart = rdtsc(); )
                                vec4 bgra = shader(face.faceAttributes,centroid);
                        profile( int64 userEnd = rdtsc(); userTime += userEnd-userStart; )
                        float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                        vec16& dstB = tile.subblue[pixelPtr];
                        vec16& dstG = tile.subgreen[pixelPtr];
                        vec16& dstR = tile.subred[pixelPtr];
                        if(blend) {
                            maskstore(dstB, visibleMask, (1-srcA)*dstB+srcA*srcB);
                            maskstore(dstG, visibleMask, (1-srcA)*dstG+srcA*srcG);
                            maskstore(dstR, visibleMask, (1-srcA)*dstR+srcA*srcR);
                        } else {
                            maskstore(dstB, visibleMask, srcB);
                            maskstore(dstG, visibleMask, srcG);
                            maskstore(dstR, visibleMask, srcR);
                        }
                        profile( sampleOverTime += (userStart-start) + (rdtsc()-userEnd); )
                    }
                }
                profile( this->userTime += userTime; sampleTime += rdtsc()-start - userTime; )
            }
        }
    }

    /// Renders all tiles
    RenderTarget* target;
    uint nextBin;
    static void* start_routine(void* this_) { ((RenderPass*)this_)->run(); return 0; }
    // For each bin, rasterizes and shade all triangles
    void render(RenderTarget& target) {
        if(!bins || !faces) return;
        this->target = &target;
        // Reset counters
        nextBin=0;
        rasterTime=0, pixelTime=0, sampleTime=0, sampleFirstTime=0, sampleOverTime=0, userTime=0, totalTime=0;
        // Schedules all cores to process tiles
        const int N=8;
        pthread threads[N-1];
        for(int i=0;i<N-1;i++) pthread_create(&threads[i],0,start_routine,this);
        run();
        for(int i=0;i<N-1;i++) { void* status; pthread_join(threads[i],&status); }
    }
    void run() {
        profile( int64 start = rdtsc(); );
        // Loop on all bins (64x64 samples (16x16 pixels)) then on passes (improve framebuffer access, passes have less locality)
        for(;;) {
            uint binI = __sync_fetch_and_add(&nextBin,1);
            if(binI>=width*height) break;
            if(!bins[binI].faceCount) continue;

            Tile& tile = target->tiles[binI];
            if(!tile.cleared) {
                clear(tile.subsample,16);
                clear(tile.depth,4*4,vec16(target->depth));
                clear(tile.blue,4*4,vec16(target->blue));
                clear(tile.green,4*4,vec16(target->green));
                clear(tile.red,4*4,vec16(target->red));
                tile.cleared=1;
            }

            const vec2 binXY = 64.f*vec2(binI%target->width,binI/target->width);
            render(tile,bins[binI],binXY);
        }
        profile( totalTime += rdtsc()-start; );
    }
};
