#pragma once
/// \file raster.h 3D rasterizer (polygon, circle)
#include "matrix.h"
#include "function.h"
#include "time.h"
#define __AVX__ 1
#include "immintrin.h"
#ifndef __GXX_EXPERIMENTAL_CXX0X__ //for QtCreator
#include "avxintrin.h"
#endif

/// 64×64 pixels bin for L1 cache locality (64×64×RGBZ×float~64KB)
struct Bin { // 16KB triangles (stream) + 64KB framebuffer (L1)
    uint16 cleared=0;
    uint16 faceCount=0;
    uint16 subsample[16]; //Per-pixel flag to trigger subpixel operations
    uint16 faces[2*64*64-2-16-2*16*16]; // maximum virtual capacity
    float nearestSampleDepth[16*16];
    /*float depth[16*16], blue[16*16], green[16*16], red[16*16];
    float subDepth[64*64], subBlue[64*64], subGreen[64*64], subRed[64*64];*/
    float buffer[4*16*16+4*64*64]; //silence array-bounds errors;
};

/// Tiled render target
struct RenderTarget {
    int2 size = 0; //in pixels
    uint width = 0, height = 0; //in bins
    Bin* bins = 0;
    float depth, blue, green, red;

    // Allocates all bins and flags them to be cleared before first render
    void resize(int2 size) {
        this->size=size;
        if(bins) unallocate(bins,width*height);
        width = align(64,size.x*4)/64;
        height = align(64,size.y*4)/64;
        bins = allocate16<Bin>(width*height);
    }

    void clear(float depth=-0x1p16f, float blue=1, float green=1, float red=1) {
        this->depth=depth;
        this->blue=blue;
        this->green=green;
        this->red=red;
        for(int i: range(this->width*this->height)) { Bin& bin = bins[i]; bin.cleared=0; bin.faceCount=0; }
    }
    ~RenderTarget() { unallocate(bins,width*height); }

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

template<class FaceAttributes /*per-face constant attributes*/, int V /*per-vertex varying attributes*/, bool blend=false> struct RenderPass {
    // Rasterization "registers", varying (perspective-interpolated) vertex attributes and constant face attributes
    struct Face { //~1K (streamed)
        vec2 edges[3];
        float binReject[3], binAccept[3];
        float nearestZ;
        float blockRejectStep[3][4*4], blockAcceptStep[3][4*4];
        float pixelRejectStep[3][4*4], pixelAcceptStep[3][4*4];
        float sampleStep[3][4*4];
        vec3 iw, iz;
        vec3 varyings[V];
        FaceAttributes faceAttributes; //custom constant face attributes
    };

    RenderTarget& target;
    uint16 faceCount=0, faceCapacity;
    Face* faces;
    int64 clearTime=0, rasterTime=0, pixelTime=0, sampleTime=0, userTime=0, totalTime=0;
    //raster
    int64 subpixelRasterTime=0;
    //sample
    int64 sampleFirstTime=0, sampleFirstZTestAndCentroidTime=0, sampleFirstOutputTime=0;
    int64 sampleOverTime=0, sampleOverZTestAndCentroidTime=0, sampleOverOutputTime=0;
    RenderPass(RenderTarget& target, uint faceCapacity):target(target),faceCapacity(faceCapacity){
        faces = allocate16<Face>(faceCapacity);
    }
    ~RenderPass(){ unallocate(faces,faceCapacity); }

    // Implementation is inline to allow per-pass face attributes specialization and inline shader calls

    virtual vec4 shader(FaceAttributes,float[V])=0;

    /// Submits triangles for bin binning, actual rendering is deferred until render
    /// \note Device coordinates are not normalized, positions should be in [0..Width],[0..Height]
    void submit(vec4 A, vec4 B, vec4 C, vec3 vertexAttributes[V], FaceAttributes faceAttributes) {
        if(faceCount>=faceCapacity) error("Face overflow");
        Face& face = faces[faceCount];
        assert(A.w==1); assert(B.w==1); assert(C.w==1); //TODO: clip planes
        mat3 E = mat3(A.xyw(), B.xyw(), C.xyw());
        float det = E.det();
        if(det<=1) return; //small or back-facing triangle
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
                for(int i=0;i<4*4;i++) { //TODO: vectorize
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
        face.nearestZ = max(max(A.z,B.z),C.z);
        for(int i=0;i<V;i++) face.varyings[i] = E*vertexAttributes[i];
        face.faceAttributes=faceAttributes;

        int2 min = ::max(int2(0,0),int2(floor(::min(::min(A.xy(),B.xy()),C.xy())))/64);
        int2 max = ::min(int2(target.width-1,target.height-1),int2(ceil(::max(::max(A.xy(),B.xy()),C.xy())))/64);

        for(int binY=min.y; binY<=max.y; binY++) for(int binX=min.x; binX<=max.x; binX++) {
            const vec2 binXY = 64.f*vec2(binX, binY);

            // trivial reject
            if(
                    face.binReject[0] + dot(face.edges[0], binXY) <= 0 ||
                    face.binReject[1] + dot(face.edges[1], binXY) <= 0 ||
                    face.binReject[2] + dot(face.edges[2], binXY) <= 0) continue;

            Bin& bin = target.bins[binY*target.width+binX];
            if(bin.faceCount>=sizeof(bin.faces)/sizeof(uint16)) error("Index overflow");
            bin.faces[bin.faceCount++]=faceCount;
        }
        faceCount++;
    }

    // For each bin, rasterizes and shade all triangles
    void render() {
        int64 start = rdtsc();
        // TODO: multithreaded tiles (4×4 bins (64×64 pixels))
        // Loop on all bins (64x64 samples (16x16 pixels))
        for(uint binI=0; binI<target.height; binI++) for(uint binJ=0; binJ<target.width; binJ++) {
            Bin& bin = target.bins[binI*target.width+binJ];
            if(!bin.faceCount) continue;
            if(!bin.cleared) {
                int64 start=rdtsc();
                clear(bin.subsample,16);
                clear(bin.buffer+0*16*16,16*16,target.depth);
                clear(bin.buffer+1*16*16,16*16,target.blue);
                clear(bin.buffer+2*16*16,16*16,target.green);
                clear(bin.buffer+3*16*16,16*16,target.red);
                bin.cleared=1;
                clearTime += rdtsc()-start;
            }
            uint16* const subsample = bin.subsample;
            float* const buffer = bin.buffer;
            const vec2 binXY = 64.f*vec2(binJ,binI);
            // Loop on all faces in the bin
            for(uint faceI=0; faceI<bin.faceCount; faceI++) {
                struct DrawCommand { vec2 pos; uint ptr; uint mask; } blocks[4*4], pixels[16*16]; uint blockCount=0, pixelCount=0;
                const Face& face = faces[bin.faces[faceI]];
                {
                    int64 start=rdtsc();
                    float binReject[3], binAccept[3];
                    for(int e=0;e<3;e++) {
                        binReject[e] = face.binReject[e] + dot(face.edges[e], binXY);
                        binAccept[e] = face.binAccept[e] + dot(face.edges[e], binXY);
                    }

                    // TODO: trivial edge accept

                    // Full bin accept
                    if(
                            binAccept[0] > 0 &&
                            binAccept[1] > 0 &&
                            binAccept[2] > 0 ) {
                        for(;blockCount<16;blockCount++)
                            blocks[blockCount] = DrawCommand __(binXY+16.f*XY[0][blockCount], blockCount*(4*4), 0xFFFF);
                    } else {
                        // Loop on 4×4 blocks
                        for(uint blockI=0; blockI<4*4; blockI++) {
                            float blockReject[3]; for(int e=0;e<3;e++) blockReject[e] = binReject[e] + face.blockRejectStep[e][blockI];

                            // trivial reject
                            if((blockReject[0] <= 0) || (blockReject[1] <= 0) || (blockReject[2] <= 0) ) continue;

                            // TODO: trivial edge accept

                            const uint16 blockPtr = blockI*(4*4);
                            const vec2 blockXY = binXY+16.f*XY[0][blockI];

                            float blockAccept[3]; for(int e=0;e<3;e++) blockAccept[e] = binAccept[e] + face.blockAcceptStep[e][blockI];
                            // Full block accept
                            if(
                                    blockAccept[0] > 0 &&
                                    blockAccept[1] > 0 &&
                                    blockAccept[2] > 0 ) {
                                blocks[blockCount++] = DrawCommand __(blockXY, blockPtr, 0xFFFF);
                                continue;
                            }

                            // Loop on 4×4 pixels
                            uint16 partialBlockMask=0; // partial block of full pixels
                            for(uint pixelI=0; pixelI<4*4; pixelI++) {
                                float pixelReject[3]; for(int e=0;e<3;e++) pixelReject[e] = blockReject[e] + face.pixelRejectStep[e][pixelI];

                                // trivial reject
                                if(pixelReject[0] <= 0 || pixelReject[1] <= 0 || pixelReject[2] <= 0) continue;

#if 0
                                // Accept any trivial edge
                                float const* sampleStep[3]; int edgeCount=0;
                                if(blockAccept[0] <= face.pixelAcceptStep[0][pixelI])
                                    pixelReject[edgeCount]=pixelReject[0], sampleStep[edgeCount]=face.sampleStep[0], edgeCount++;
                                if(blockAccept[1] <= face.pixelAcceptStep[1][pixelI])
                                    pixelReject[edgeCount]=pixelReject[1], sampleStep[edgeCount]=face.sampleStep[1], edgeCount++;
                                if(blockAccept[2] <= face.pixelAcceptStep[2][pixelI])
                                    pixelReject[edgeCount]=pixelReject[2], sampleStep[edgeCount]=face.sampleStep[2], edgeCount++;

                                // Full pixel accept
                                if(edgeCount==0) {
                                    partialBlockMask |= 1<<pixelI;
                                    continue;
                                }

                                uint16 mask=0;
                                if(edgeCount==1) {
                                    // Loop on 4×4 samples
                                    for(uint sampleI=0; sampleI<16; sampleI++) {
                                        if( pixelReject[0] <= sampleStep[0][sampleI] ) continue;
                                        mask |= (1<<sampleI);
                                    }
                                } else if(edgeCount==2) {
                                    // Loop on 4×4 samples
                                    for(uint sampleI=0; sampleI<16; sampleI++) {
                                        if(
                                                pixelReject[0] <= sampleStep[0][sampleI] ||
                                                pixelReject[1] <= sampleStep[1][sampleI] ) continue;
                                        mask |= (1<<sampleI);
                                    }
                                } else {
                                    // Loop on 4×4 samples
                                    for(uint sampleI=0; sampleI<16; sampleI++) {
                                        if(
                                                pixelReject[0] <= sampleStep[0][sampleI] ||
                                                pixelReject[1] <= sampleStep[1][sampleI] ||
                                                pixelReject[2] <= sampleStep[2][sampleI] ) continue;
                                        mask |= (1<<sampleI);
                                    }
                                }
#else
                                // Full pixel accept
                                if(
                                        blockAccept[0] <= face.pixelAcceptStep[0][pixelI] &&
                                        blockAccept[1] <= face.pixelAcceptStep[1][pixelI] &&
                                        blockAccept[2] <= face.pixelAcceptStep[2][pixelI] ) {
                                    partialBlockMask |= 1<<pixelI;
                                    continue;
                                }

                                uint16 mask=0;
                                {
                                    int64 start = rdtsc();
                                    // Loop on 4×4 samples
#if __AVX__ && 0
                                    typedef float float8 __attribute__ ((vector_size(32),may_alias));
                                    float8 pixelReject0 = _mm256_broadcast_ss(&pixelReject[0]);
                                    mask = _mm256_movemask_ps(_mm256_cmp_ps(pixelReject0, (float8&)face.sampleStep[0][0*4], _CMP_LE_OQ))&0xFF;
                                    mask |= (_mm256_movemask_ps(_mm256_cmp_ps(pixelReject0, (float8&)face.sampleStep[0][2*4], _CMP_LE_OQ))&0xFF)<<8;
#else

                                    for(uint sampleI=0; sampleI<16; sampleI++) {
                                        if(
                                                pixelReject[0] <= face.sampleStep[0][sampleI] ||
                                                pixelReject[1] <= face.sampleStep[1][sampleI] ||
                                                pixelReject[2] <= face.sampleStep[2][sampleI] ) continue;
                                        mask |= (1<<sampleI);
                                    }
#endif
                                    subpixelRasterTime += rdtsc()-start;
                                }
#endif
                                const vec2 pixelXY = 4.f*XY[0][pixelI];
                                const uint16 pixelPtr = blockPtr+pixelI;
                                pixels[pixelCount++] = DrawCommand __(blockXY+pixelXY, pixelPtr, mask);
                            }
                            if(partialBlockMask)
                                blocks[blockCount++] = DrawCommand __(blockXY, blockPtr, partialBlockMask);
                            //else all pixels were subsampled
                        }
                    }
                    rasterTime += rdtsc()-start;
                }

                {
                    int64 start = rdtsc();
                    int64 userTime=0;
                    for(uint i=0; i<blockCount; i++) { //Blocks of fully covered pixels
                        const DrawCommand& draw = blocks[i];
                        const uint blockPtr = draw.ptr;
                        const vec2 blockXY = draw.pos;
                        const uint16 mask = draw.mask;
                        for(uint pixelI=0; pixelI<4*4; pixelI++) {
                            if(!(mask&(1<<pixelI))) continue; //vectorized code might actually mask output instead
                            const uint pixelPtr = blockPtr+pixelI;
                            const vec2 pixelXY = blockXY+4.f*XY[0][pixelI];

                            /*vec3 XY1 = vec3(pixelXY+vec2(4.f/2, 4.f/2), 1.f);
                            float w = 1/dot(face.iw,XY1);
                            float z = w*dot(face.iz,XY1);

                            float* const pixel = buffer+pixelPtr;
                            float& depth = *pixel; //furthest
                            if(z < depth) {
                                //pixel[1*16*16]=1; //DEBUG
                                continue; //Hi-Z reject
                            }*/

                            float& nearest = *(buffer-16*16+pixelPtr);

                            if(!(subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) { // Pixel coverage on single sample pixel
                                vec3 XY1 = vec3(pixelXY+vec2(4.f/2, 4.f/2), 1.f);
                                float w = 1/dot(face.iw,XY1);
                                float z = w*dot(face.iz,XY1);

                                float* const pixel = buffer+pixelPtr;
                                float& depth = *pixel; //furthest
                                if(z < depth) {
                                    //pixel[1*16*16]=1; //DEBUG
                                    continue; //Hi-Z reject
                                }
                                depth = z;
                                float centroid[V]; for(int i=0;i<V;i++) centroid[i]=w*dot(face.varyings[i],XY1);
                                int64 start = rdtsc();
                                vec4 bgra = shader(face.faceAttributes,centroid);
                                userTime += rdtsc()-start;
                                float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA=bgra.w;
                                float& dstB = pixel[1*16*16];
                                float& dstG = pixel[2*16*16];
                                float& dstR = pixel[3*16*16];
                                if(blend) {
                                    dstB=(1-srcA)*dstB+srcA*srcB;
                                    dstG=(1-srcA)*dstG+srcA*srcG;
                                    dstR=(1-srcA)*dstR+srcA*srcR;
                                } else {
                                    dstB=srcB;
                                    dstG=srcG;
                                    dstR=srcR;
                                }
                            }
                            /*else if(z >= *(buffer-16*16+pixelPtr)) { // Full Z accept
                                subsample[pixelPtr/16] &= ~(1<<(pixelPtr%16)); // Clear subsample flag

                                depth = z;
                                float centroid[V]; for(int i=0;i<V;i++) centroid[i]=w*dot(face.varyings[i],XY1);
                                int64 start = rdtsc();
                                vec4 bgra = shader(face.faceAttributes,centroid);
                                userTime += rdtsc()-start;
                                float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA=bgra.w;
                                float& dstB = pixel[1*16*16];
                                float& dstG = pixel[2*16*16];
                                float& dstR = pixel[3*16*16];
                                if(blend) {
                                    float* const subpixel = buffer+4*16*16+pixelPtr*4*4;
                                    // Resolve pixel for blending
                                    float avgB=0, avgG=0, avgR=0;
                                    for(uint sampleI=0; sampleI<16; sampleI++) {
                                        float* const sample = subpixel+sampleI;
                                        avgB += sample[1*64*64];
                                        avgG += sample[2*64*64];
                                        avgR += sample[3*64*64];
                                    }
                                    avgB /= 4*4, avgG /=4*4, avgR /= 4*4;
                                    dstB=(1-srcA)*avgB+srcA*srcB;
                                    dstG=(1-srcA)*avgG+srcA*srcG;
                                    dstR=(1-srcA)*avgR+srcA*srcR;
                                } else {
                                    dstB=srcB;
                                    dstG=srcG;
                                    dstR=srcR;
                                }
                                //dstG=1; //DEBUG: coalesced pixel
                            }*/
                            else { // Partial Z mask
                                float* const subpixel = buffer+4*16*16+pixelPtr*4*4;
                                uint mask=0; float centroid[V] = {}; float samples=0;
                                // Loop on 4×4 samples
                                for(uint sampleI=0; sampleI<16; sampleI++) {
                                    const vec2 sampleXY = XY[0][sampleI]; //TODO: latin square pattern

                                    vec3 XY1 = vec3(pixelXY+sampleXY+vec2(1.f/2, 1.f/2), 1.f);
                                    float w = 1/dot(face.iw,XY1);
                                    float z = w*dot(face.iz,XY1);

                                    float* const sample = subpixel+sampleI;
                                    float& depth = *sample;
                                    if(z>=depth) {
                                        depth = z;
                                        if(z > nearest) nearest=z;
                                        samples++;
                                        mask |= (1<<sampleI);
                                        for(int i=0;i<V;i++) centroid[i]+=w*dot(face.varyings[i],XY1);
                                    }
                                }

                                for(uint i=0;i<V;i++) centroid[i] /= samples;
                                int64 start = rdtsc();
                                vec4 bgra = shader(face.faceAttributes,centroid);
                                userTime += rdtsc()-start;
                                float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                                for(uint sampleI=0;sampleI<16;sampleI++) {
                                    float* const sample = subpixel+sampleI;
                                    if(mask&(1<<sampleI)) {
                                        float& dstB = sample[1*64*64];
                                        float& dstG = sample[2*64*64];
                                        float& dstR = sample[3*64*64];
                                        if(blend) {
                                            dstB=(1-srcA)*dstB+srcA*srcB;
                                            dstG=(1-srcA)*dstG+srcA*srcG;
                                            dstR=(1-srcA)*dstR+srcA*srcR;
                                        } else {
                                            dstB=srcB;
                                            dstG=srcG;
                                            dstR=srcR;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    this->userTime+=userTime;
                    pixelTime += rdtsc()-start - userTime;
                }
                {
                    int64 start = rdtsc();
                    int64 userTime=0;
                    for(uint i=0; i<pixelCount; i++) { // Partially covered pixel of samples
                        const DrawCommand& draw = pixels[i];
                        const uint pixelPtr = draw.ptr;
                        const vec2 pixelXY = draw.pos;
                        float* const subpixel = buffer+4*16*16+pixelPtr*4*4;
                        uint16 mask = draw.mask;
                        float& nearest = *(buffer-16*16+pixelPtr);

                        // Convert single sample pixel to subsampled pixel
                        if(!(subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                            float* const pixel = buffer+pixelPtr;

                            // early Z reject to avoid hidden MSAA splits
                            /*if(face.nearestZ < *pixel) {
                                continue; // Hi-Z reject
                            }*/

                            // Set subsampled pixel flag
                            subsample[pixelPtr/16] |= (1<<(pixelPtr%16));

                            nearest = *pixel;
                            float centroid[V] = {}; float samples=0;
                            {
                                int64 start = rdtsc();
                                // Loop on 4×4 samples
                                for(uint sampleI=0; sampleI<16; sampleI++) {
                                    float* const sample = subpixel+sampleI;
                                    if(!(mask&(1<<sampleI))) { *sample=*pixel; continue; } //vectorized code might actually mask output instead

                                    const vec2 sampleXY = XY[0][sampleI]; //TODO: latin square pattern
                                    vec3 XY1 = vec3(pixelXY+sampleXY+vec2(1.f/2, 1.f/2), 1.f);
                                    float w = 1/dot(face.iw,XY1);
                                    float z = w*dot(face.iz,XY1);

                                    if(z<*pixel) { mask &= ~(1<<sampleI); *sample=*pixel; continue; }
                                    *sample = z;
                                    if(z > nearest) nearest=z;
                                    samples++;
                                    for(int i=0;i<V;i++) centroid[i]+=w*dot(face.varyings[i],XY1);
                                }
                                sampleFirstZTestAndCentroidTime += rdtsc()-start;
                            }
                            {
                                for(uint i=0;i<V;i++) centroid[i] /= samples;
                                int64 userStart = rdtsc();
                                vec4 bgra = shader(face.faceAttributes,centroid);
                                uint64 userEnd = rdtsc();
                                userTime += userEnd-userStart;
                                float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                                for(uint sampleI=0;sampleI<16;sampleI++) {
                                    float* const sample = subpixel+sampleI;
                                    float& dstB = sample[1*64*64];
                                    float& dstG = sample[2*64*64];
                                    float& dstR = sample[3*64*64];
                                    if(mask&(1<<sampleI)) {
                                        if(blend) {
                                            float pixelB = pixel[1*16*16];
                                            float pixelG = pixel[2*16*16];
                                            float pixelR = pixel[3*16*16];
                                            dstB=(1-srcA)*pixelB+srcA*srcB;
                                            dstG=(1-srcA)*pixelG+srcA*srcG;
                                            dstR=(1-srcA)*pixelR+srcA*srcR;
                                        } else {
                                            dstB=srcB;
                                            dstG=srcG;
                                            dstR=srcR;
                                        }
                                    } else {
                                        float pixelB = pixel[1*16*16];
                                        float pixelG = pixel[2*16*16];
                                        float pixelR = pixel[3*16*16];
                                        dstB=pixelB;
                                        dstG=pixelG;
                                        dstR=pixelR;
                                    }
                                }
                                sampleFirstOutputTime += rdtsc()-userEnd;
                            }
                            continue;
                        }

                        // early Z reject to avoid MSAA overdraw
                        /*if(face.nearestZ < nearest) {
                            continue; // Hi-Z reject
                        }*/

                        float centroid[V] = {}; float samples=0;
                        {
                            int64 start = rdtsc();
                            // Loop on 4×4 samples
                            for(uint sampleI=0; sampleI<16; sampleI++) {
                                if(!(mask&(1<<sampleI))) continue; //vectorized code might actually mask output instead

                                const vec2 sampleXY = XY[0][sampleI]; //TODO: latin square pattern
                                vec3 XY1 = vec3(pixelXY+sampleXY+vec2(1.f/2, 1.f/2), 1.f);
                                float w = 1/dot(face.iw,XY1);
                                float z = w*dot(face.iz,XY1);

                                float* const sample = subpixel+sampleI;
                                float& depth = *sample;
                                if(z < depth) { mask &= ~(1<<sampleI); continue; }
                                if(z > nearest) nearest=z;
                                depth = z;
                                samples++;
                                for(int i=0;i<V;i++) centroid[i]+=w*dot(face.varyings[i],XY1);
                            }
                            sampleOverZTestAndCentroidTime += rdtsc()-start;
                        }
                        {
                            for(uint i=0;i<V;i++) centroid[i] /= samples;
                            int64 userStart = rdtsc();
                            vec4 bgra = shader(face.faceAttributes,centroid);
                            uint64 userEnd = rdtsc();
                            userTime += userEnd-userStart;
                            float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                            for(uint sampleI=0;sampleI<16;sampleI++) {
                                float* const sample = subpixel+sampleI;
                                if(mask&(1<<sampleI)) {
                                    float& dstB = sample[1*64*64];
                                    float& dstG = sample[2*64*64];
                                    float& dstR = sample[3*64*64];
                                    if(blend) {
                                        dstB=(1-srcA)*dstB+srcA*srcB;
                                        dstG=(1-srcA)*dstG+srcA*srcG;
                                        dstR=(1-srcA)*dstR+srcA*srcR;
                                    } else {
                                        dstB=srcB;
                                        dstG=srcG;
                                        dstR=srcR;
                                    }
                                    //dstR=1; //DEBUG
                                }
                            }
                            sampleOverOutputTime += rdtsc()-userEnd;
                        }
                    }
                    this->userTime += userTime;
                    sampleTime += rdtsc()-start - userTime;
                }
            }
            bin.faceCount=0;
        }
        faceCount=0;
        sampleFirstTime = sampleFirstZTestAndCentroidTime+sampleFirstOutputTime;
        sampleOverTime = sampleOverZTestAndCentroidTime+sampleOverOutputTime;
        totalTime = rdtsc()-start;
    }
};

