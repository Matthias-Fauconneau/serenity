#pragma once
/// \file raster.h 3D rasterizer (polygon, circle)
#include "matrix.h"
#include "function.h"

template<class FaceAttributes, int V> using Shader = functor<vec4(FaceAttributes,float[V])>;

/// 64×64 pixels bin for L1 cache locality (64×64×RGBZ×float~64KB)
struct Bin { // 16KB triangles (stream) + 64KB framebuffer (L1)
    uint16 cleared=0;
    uint16 faceCount=0;
    uint16 subsample[16]; //Per-pixel flag to trigger subpixel operations
    uint16 faces[2*64*64-2-16]; // maximum virtual capacity
    float depth[64*64], blue[64*64], green[64*64], red[64*64];
};

/// Tiled render target
struct RenderTarget {
    uint width,height; //in bins
    Bin* bins;
    float depth, blue, green, red;

    // Allocates all bins and flags them to be cleared before first render
    RenderTarget(uint width, uint height, float depth=-0x1p16f, float blue=1, float green=1, float red=1)
        : width(align(64,width)/64),
          height(align(64,height)/64),
          bins(allocate16<Bin>(this->width*this->height)),
          depth(depth), blue(blue), green(green), red(red) {
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
        int binReject[3], binAccept[3];
        int blockRejectStep[3][4*4], blockAcceptStep[3][4*4];
        int pixelRejectStep[3][4*4], pixelAcceptStep[3][4*4];
        int sampleStep[3][4*4];
        vec3 iw, iz;
        vec3 varyings[V];
        FaceAttributes faceAttributes; //custom constant face attributes
    };

    RenderTarget& target;
    uint16 faceCount=0, faceCapacity;
    Face* faces;
    RenderPass(RenderTarget& target, uint faceCapacity):target(target),faceCapacity(faceCapacity){
        faces = allocate16<Face>(faceCapacity);
    }
    ~RenderPass(){ unallocate(faces,faceCapacity); }

    // Implementation is inline to allow per-pass face attributes specialization

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
                int step0 = (edge.x>0?edge.x:0) + (edge.y>0?edge.y:0);
                // initial reject corner distance
                face.binReject[e] = E[e].z + 64.f*step0;
                // 4×4 reject corner steps
                int rejectIndex = (edge.x>0)*2 + (edge.y>0);
                for(int i=0;i<4*4;i++) { //TODO: vectorize
                    int step = dot(edge, XY[rejectIndex][i]);
                    face.blockRejectStep[e][i] = 16.f*step;
                    face.pixelRejectStep[e][i] = 4.f*step;
                    face.sampleStep[e][i] = -(1.f*step-1.f/2*step0); //to center (reversed to allow direct comparison)
                }
            }

            { //accept
                int step0 = (edge.x<=0?edge.x:0) + (edge.y<=0?edge.y:0);
                // accept corner distance
                face.binAccept[e] = E[e].z + 64.f*step0;
                // 4×4 accept corner steps
                int acceptIndex = (edge.x<=0)*2 + (edge.y<=0);
                for(int i=0;i<4*4;i++) {
                    int step = dot(edge, XY[acceptIndex][i]);
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
        int2 max = ::min(int2(target.width-1,target.height-1),int2(ceil(::max(::max(A.xy(),B.xy()),C.xy())))/64);
        // TODO: profile average bin count, if count>=16: add a hierarchy level
        for(int binY=min.y; binY<=max.y; binY++) for(int binX=min.x; binX<=max.x; binX++) {
            const vec2 binXY = 64.f*vec2(binX, binY);

            // trivial reject
            if(
                    face.binReject[0] + dot(face.edges[0], binXY) <= 0 ||
                    face.binReject[1] + dot(face.edges[1], binXY) <= 0 ||
                    face.binReject[2] + dot(face.edges[2], binXY) <= 0) continue;

            Bin& bin = target.bins[binY*target.width+binX];
            //TODO: trivial accept
            if(bin.faceCount>=sizeof(bin.faces)/sizeof(uint16)) error("Index overflow");
            bin.faces[bin.faceCount++]=faceCount;
        }
        faceCount++;
    }

    // For each bin, rasterizes and shade all triangles using given shader
    void render(const Shader<FaceAttributes,V>& shader unused) {
        // TODO: multithreaded tiles (4×4 bins (64×64 pixels))
        // Loop on all bins (64x64 samples (16x16 pixels))
        for(uint binI=0; binI<target.height; binI++) for(uint binJ=0; binJ<target.width; binJ++) {
            Bin& bin = target.bins[binI*target.width+binJ];
            if(!bin.cleared) {
                clear(bin.subsample,16);
                clear(bin.depth,64*64,target.depth);
                clear(bin.blue,64*64,target.blue);
                clear(bin.green,64*64,target.green);
                clear(bin.red,64*64,target.red);
                bin.cleared=1;
            }
            uint16* const subsample = bin.subsample;
            float* const buffer = bin.depth;
            const vec2 binXY = 64.f*vec2(binJ,binI);
            // Loop on all faces in the bin
            for(uint faceI=0; faceI<bin.faceCount; faceI++) {
                struct DrawCommand { vec2 pos; uint ptr; uint mask; } blocks[4*4], pixels[16*16]; uint blockCount=0, pixelCount=0;
                const Face& face = faces[bin.faces[faceI]];
                int binReject[3], binAccept[3];
                for(int e=0;e<3;e++) {
                    binReject[e] = face.binReject[e] + dot(face.edges[e], binXY);
                    binAccept[e] = face.binAccept[e] + dot(face.edges[e], binXY);
                }

                // TODO: Hi-Z reject
                // TODO: trivial edge accept
                // Full bin accept
                if(
                        binAccept[0] > 0 &&
                        binAccept[1] > 0 &&
                        binAccept[2] > 0 ) {
                    for(;blockCount<16;blockCount++)
                        blocks[blockCount] = DrawCommand __(binXY+16.f*XY[0][blockCount], blockCount*(4*4), 0xFFFF);
                } else {
                    // Loop on 4×4 blocks (TODO: vectorize, loop on coverage mask bitscan)
                    for(uint blockI=0; blockI<4*4; blockI++) {
                        int blockReject[3]; for(int e=0;e<3;e++) blockReject[e] = binReject[e] + face.blockRejectStep[e][blockI];

                        // trivial reject
                        if((blockReject[0] <= 0) || (blockReject[1] <= 0) || (blockReject[2] <= 0) ) continue;

                        // TODO: Hi-Z reject
                        // TODO: trivial edge accept

                        const uint16 blockPtr = blockI*(4*4); //not (4*4)² since samples are planar
                        const vec2 blockXY = binXY+16.f*XY[0][blockI];

                        int blockAccept[3]; for(int e=0;e<3;e++) blockAccept[e] = binAccept[e] + face.blockAcceptStep[e][blockI];
                        // Full block accept
                        if(
                                blockAccept[0] > 0 &&
                                blockAccept[1] > 0 &&
                                blockAccept[2] > 0 ) {
                            blocks[blockCount++] = DrawCommand __(blockXY, blockPtr, 0xFFFF);
                            continue;
                        }

                        // Loop on 4×4 pixels (TODO: vectorize, loop on coverage mask bitscan)
                        uint16 partialBlockMask=0; // partial block of full pixels
                        for(uint pixelI=0; pixelI<4*4; pixelI++) {
                            // trivial reject
                            if(
                                    blockReject[0] + face.pixelRejectStep[0][pixelI] <= 0 ||
                                    blockReject[1] + face.pixelRejectStep[1][pixelI] <= 0 ||
                                    blockReject[2] + face.pixelRejectStep[2][pixelI] <= 0 ) continue;

                            // TODO: Hi-Z reject
                            // TODO: trivial edge accept

                            // Full pixel accept
                            if(
                                    blockAccept[0] > face.pixelAcceptStep[0][pixelI] &&
                                    blockAccept[1] > face.pixelAcceptStep[1][pixelI] &&
                                    blockAccept[2] > face.pixelAcceptStep[2][pixelI] ) {
                                partialBlockMask |= 1<<pixelI;
                                continue;
                            }

                            int pixelReject[3]; for(int e=0;e<3;e++) pixelReject[e] = blockReject[e] + face.pixelRejectStep[e][pixelI];

                            uint16 mask=0;
                            // Loop on 4×4 samples (TODO: vectorize, loop on coverage mask bitscan)
                            for(uint sampleI=0; sampleI<16; sampleI++) {
                                if(
                                        pixelReject[0] <= face.sampleStep[0][sampleI] ||
                                        pixelReject[1] <= face.sampleStep[1][sampleI] ||
                                        pixelReject[2] <= face.sampleStep[2][sampleI] ) continue;
                                mask |= (1<<sampleI);
                            }
                            const vec2 pixelXY = 4.f*XY[0][pixelI];
                            const uint16 pixelPtr = blockPtr+pixelI; //not pixel*(4*4) since samples are planar
                            pixels[pixelCount++] = DrawCommand __(blockXY+pixelXY, pixelPtr, mask);
                        }
                        blocks[blockCount++] = DrawCommand __(blockXY, blockPtr, partialBlockMask);
                    }
                }

                assert(blockCount<=16);
                for(uint i=0; i<blockCount; i++) { //Blocks of fully covered pixels
                    const DrawCommand& draw = blocks[i];
                    const uint blockPtr = draw.ptr;
                    const vec2 blockXY = draw.pos;
                    const uint16 mask = draw.mask;
                    for(uint pixelI=0; pixelI<4*4; pixelI++) {
                        if(!(mask&(1<<pixelI))) continue; //vectorized code might actually mask output instead
                        const uint pixelPtr = blockPtr+pixelI; //not pixel*(4*4) since samples are planar
                        const vec2 pixelXY = 4.f*XY[0][pixelI];
                        float* const pixel = buffer+pixelPtr;
                        // Pixel coverage on single sample pixel
                        if(1/*!(subsample[pixelPtr/16]&(1<<(pixelPtr%16)))*/) {
                            vec3 XY1 = vec3(blockXY+pixelXY+vec2(4.f/2, 4.f/2), 1.f);
                            float w = 1/dot(face.iw,XY1);
                            float z = w*dot(face.iz,XY1);

                            float& depth = *pixel;
                            if(z>=depth) {
                                float centroid[V]; for(int i=0;i<V;i++) centroid[i]=w*dot(face.varyings[i],XY1);
                                //vec4 bgra = vec4(0,0,0,1); //DEBUG
                                vec4 bgra = shader(face.faceAttributes,centroid);
                                float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA=bgra.w;
                                float& dstB = pixel[1*64*64];
                                float& dstG = pixel[2*64*64];
                                float& dstR = pixel[3*64*64];
                                if(blend) {
                                    dstB=(1-srcA)*dstB+srcA*srcB;
                                    dstG=(1-srcA)*dstG+srcA*srcG;
                                    dstR=(1-srcA)*dstR+srcA*srcR;
                                } else {
                                    dstB=srcB;
                                    dstG=srcG;
                                    dstR=srcR;
                                }
                                dstG=1; //DEBUG: non-multisampled pixel
                            }
                        }
                        else // Pixel coverage on subsampled pixel
                        {
                            uint mask=0; float centroid[V] = {}; float samples=0;
                            // Loop on 4×4 samples (TODO: vectorize, loop on coverage mask bitscan)
                            for(uint sampleI=0; sampleI<16; sampleI++) {
                                float* const sample = 16*16*sampleI+pixel; //planar samples (free cache on mostly non-subsampled patterns)
                                const vec2 sampleXY = XY[0][sampleI]; //TODO: latin square pattern

                                vec3 XY1 = vec3(pixelXY+sampleXY+vec2(1.f/2, 1.f/2), 1.f);
                                float w = 1/dot(face.iw,XY1);
                                float z = w*dot(face.iz,XY1);

                                float& depth = *sample;
                                if(z>=depth) {
                                    depth = z;
                                    samples++;
                                    mask |= 1<<sampleI;
                                    for(int i=0;i<V;i++) centroid[i]+=w*dot(face.varyings[i],XY1);
                                }
                            }

                            for(uint i=0;i<V;i++) centroid[i] /= samples;
                            //vec4 bgra = vec4(0,0,0,1); //DEBUG
                            vec4 bgra = shader(face.faceAttributes,centroid);
                            float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                            // Occluded subsampled pixel become whole again
                            if(mask==(1<<16)-1) { //TODO: Hi-Z full pixel accept to avoid 16 Z-test + interpolation
                                subsample[pixelPtr/16] &= ~(1<<(pixelPtr%16));
                                float& dstB = pixel[1*64*64];
                                float& dstG = pixel[2*64*64];
                                float& dstR = pixel[3*64*64];
                                if(blend) {
                                    dstB=(1-srcA)*dstB+srcA*srcB;
                                    dstG=(1-srcA)*dstG+srcA*srcG;
                                    dstR=(1-srcA)*dstR+srcA*srcR;
                                } else {
                                    dstB=srcB;
                                    dstG=srcG;
                                    dstR=srcR;
                                }
                                //dstB=1; //DEBUG: coalesced pixel
                            } else {
                                for(uint sampleI=0;sampleI<16;sampleI++) {
                                    float* const sample = 16*16*sampleI+pixel;
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
                }

                assert(pixelCount<=16*16);
                for(uint i=0; i<pixelCount; i++) { //Partially covered Pixel of samples
                    const DrawCommand& draw = pixels[i];
                    const uint pixelPtr = draw.ptr; assert(pixelPtr/16<16);
                    const vec2 pixelXY = draw.pos;
                    float* const pixel = buffer+pixelPtr;
                    uint16 mask = draw.mask;

                    // Convert single sample pixel to subsampled pixel
                    if(!(subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                        // Set subsampled pixel flag
                        subsample[pixelPtr/16] |= (1<<(pixelPtr%16));
                        // Broadcast pixel (first) sample to all (other) subsamples
                        for(uint sampleI=1;sampleI<16;sampleI++) {
                            float* const sample = 16*16*sampleI+pixel;
                            sample[0*64*64]=pixel[0*64*64];
                            sample[1*64*64]=pixel[1*64*64];
                            sample[2*64*64]=pixel[2*64*64];
                            sample[3*64*64]=pixel[3*64*64];
                        }
                    }

                    float centroid[V] = {}; float samples=0;
                    // Loop on 4×4 samples (TODO: vectorize, loop on coverage mask bitscan)
                    for(uint sampleI=0; sampleI<16; sampleI++) {
                        if(!(mask&(1<<sampleI))) continue; //vectorized code might actually mask output instead

                        const vec2 sampleXY = XY[0][sampleI]; //TODO: latin square pattern
                        vec3 XY1 = vec3(pixelXY+sampleXY+vec2(1.f/2, 1.f/2), 1.f);
                        float w = 1/dot(face.iw,XY1);
                        float z = w*dot(face.iz,XY1);

                        float* const sample = 16*16*sampleI+pixel; //planar samples (free cache on mostly non-subsampled patterns)
                        float& depth = *sample;
                        if(z<depth) { mask &= ~(1<<sampleI); continue; }
                        depth = z;
                        samples++;
                        for(int i=0;i<V;i++) centroid[i]+=w*dot(face.varyings[i],XY1);
                    }

                    for(uint i=0;i<V;i++) centroid[i] /= samples;
                    //vec4 bgra = vec4(0,0,0,1); //DEBUG
                    vec4 bgra = shader(face.faceAttributes,centroid);
                    float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                    for(uint sampleI=0;sampleI<16;sampleI++) {
                        float* const sample = 16*16*sampleI+pixel;
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
            bin.faceCount=0;
        }
        faceCount=0;
    }
};

