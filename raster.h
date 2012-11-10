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

template<class FaceAttributes /*per-face constant attributes*/, int V /*per-vertex varying attributes*/, bool blend=false> struct RenderPass {
    // triangle face with 3F constant face attributes and V varying (perspective-interpolated) vertex attributes
    struct VertexAttributes { vec3 data[V]; };
    struct Face {
        mat3 E; //edge equations
        vec3 Z; //vertex depth attributes
        VertexAttributes vertexAttributes; //custom vertex attributes (as a struct because fixed arrays miss value semantics)
        FaceAttributes faceAttributes; //custom constant face attributes
    };

    RenderTarget& target;
    uint16 faceCount=0;
    Face faces[1<<12]; // maximum virtual capacity
    RenderPass(RenderTarget& target):target(target){}

    // Implementation is inline to allow per-pass face attributes specialization

    /// Submits triangles for bin binning, actual rendering is deferred until render
    /// \note Device coordinates are not normalized, positions should be in [0..Width],[0..Height]
    void submit(vec4 A, vec4 B, vec4 C, VertexAttributes vertexAttributes, FaceAttributes faceAttributes) {
        assert(A.w==1); assert(B.w==1); assert(C.w==1);
        mat3 E = mat3(A.xyw(), B.xyw(), C.xyw());
        float det = E.det();
        if(det<=1) return; //small or back-facing triangle
        E = E.cofactor(); //edge equations are now columns of E
        if(E[0].x>0/*dy<0*/ || (E[0].x==0/*dy=0*/ && E[0].y<0/*dx<0*/)) E[0].z++;
        if(E[1].x>0/*dy<0*/ || (E[1].x==0/*dy=0*/ && E[1].y<0/*dx<0*/)) E[1].z++;
        if(E[2].x>0/*dy<0*/ || (E[2].x==0/*dy=0*/ && E[2].y<0/*dx<0*/)) E[2].z++;

        faces[faceCount] = Face __(E, vec3(A.z,B.z,C.z), vertexAttributes, faceAttributes);

        int2 min = ::max(int2(0,0),int2(floor(::min(::min(A.xy(),B.xy()),C.xy())))/64);
        int2 max = ::min(int2(target.width-1,target.height-1),int2(ceil(::max(::max(A.xy(),B.xy()),C.xy())))/64);
        // TODO: profile average bin count, if count>=16: add a hierarchy level
        for(int binY=min.y; binY<=max.y; binY++) for(int binX=min.x; binX<=max.x; binX++) {
            Bin& bin = target.bins[binY*target.width+binX];
            bin.faces[bin.faceCount++]=faceCount;
            if(faceCount>sizeof(bin.faces)/sizeof(uint16)) error("Index overflow");
        }
        faceCount++;
        if(faceCount>sizeof(faces)/sizeof(Face)) error("Face overflow");
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
            const vec2 binXY = vec2(binJ,binI);
            // Loop on all faces in the bin
            for(uint faceI=0; faceI<bin.faceCount; faceI++) {
                const Face& face = faces[bin.faces[faceI]];
                const mat3& E = face.E;

                //4×4 xy steps constant mask
                static constexpr vec2 XY[4*4] = {
                                                  vec2(0,0),vec2(1,0),vec2(2,0),vec2(3,0),
                                                  vec2(0,1),vec2(1,1),vec2(2,1),vec2(3,1),
                                                  vec2(0,2),vec2(1,2),vec2(2,2),vec2(3,2),
                                                  vec2(0,3),vec2(1,3),vec2(2,3),vec2(3,3),
                                                };

                // Computes bin initial distance, 4×4 edge steps and trivial reject/accept corner steps for each edge
                float step[3][4*4], binD[3], reject[3], accept[3];
                for(int e=0;e<3;e++) {
                    // Interpolation functions (-dy, dx, d)
                    const vec2 edge = E[e].xy();
                    // Bin initial distance
                    binD[e] = 1.f/64*E[e].z + dot(edge,binXY);
                    // 4×4 edge steps
                    for(int i=0;i<4*4;i++) step[e][i] = dot(edge,XY[i]);
                    // trivial reject/accept corner steps for each edge
                    int rejectIndex = (edge.y>0)*3*4 + (edge.x>0)*3;
                    int acceptIndex = ~rejectIndex;
                    reject[e] = step[e][rejectIndex];
                    accept[e] = step[e][acceptIndex];
                }

                // trivial reject
                if((binD[0] + reject[0] <= 0) || (binD[1] + reject[1] <= 0) || (binD[2] + reject[2] <= 0) ) continue;

                // TODO: Hi-Z block reject

                const vec3 iw = E[0]+E[1]+E[2];
                const vec3 iz = E*face.Z;
                vec3 varyings[V]; for(int i=0;i<V;i++) varyings[i] = E*face.vertexAttributes.data[i];

                // TODO: trivial edge accept (for bin)

                // Full tile accept

                // Scale from bin to blocks
                for(int e=0;e<3;e++) binD[e] *= 4;

                // Loop on 4×4 blocks (TODO: vectorize, loop on coverage mask bitscan)
                for(uint blockI=0; blockI<4*4; blockI++) {
                    float blockD[3]; for(int e=0;e<3;e++) blockD[e] = binD[e] + step[e][blockI];

                    // trivial reject
                    if((blockD[0] + reject[0] <= 0) || (blockD[1] + reject[1] <= 0) || (blockD[2] + reject[2] <= 0) ) continue;

                    // TODO: Hi-Z block reject

                    // TODO: trivial edge accept (for block)

                    // Full block accept

                    // Scale from blocks to pixels
                    for(int e=0;e<3;e++) blockD[e] *= 4;
                    const uint blockPtr = blockI*(4*4); //not (4*4)² since samples are planar
                    const vec2 blockXY = XY[blockI];

                    // Loop on 4×4 pixels (TODO: vectorize, loop on coverage mask bitscan)
                    for(uint pixelI=0; pixelI<4*4; pixelI++) {
                        float pixelD[3]; for(int e=0;e<3;e++) pixelD[e] = blockD[e] + step[e][pixelI];

                        // trivial reject
                        if((pixelD[0] + reject[0] <= 0) || (pixelD[1] + reject[1] <= 0) || (pixelD[2] + reject[2] <= 0) ) continue;

                        // TODO: Hi-Z pixel reject

                        const uint pixelPtr = blockPtr+pixelI; //not pixel*(4*4) since samples are planar
                        const vec2 pixelXY = XY[pixelI];
                        float* const pixel = buffer+pixelPtr;

                        // TODO: trivial edge pixel accept

                        // Full pixel accept
                        if(
                                pixelD[0] + accept[0] > 0 &&
                                pixelD[1] + accept[1] > 0 &&
                                pixelD[2] + accept[2] > 0 ) {
                            if(!(subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                                vec3 XY1 = vec3(((binXY*4.f+blockXY)*4.f+pixelXY+vec2(1.f/2, 1.f/2))*4.f, 1.f);
                                float w = 1/dot(iw,XY1);
                                float z = w*dot(iz,XY1);

                                float& depth = *pixel;
                                if(z>=depth) {
                                    float centroid[V]; for(int i=0;i<V;i++) centroid[i]=w*dot(varyings[i],XY1);
                                    vec4 bgra = vec4(0,0,0,1); //shader(face.faceAttributes,centroid);
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
                            } else { // Full coverage accept
                                uint mask=0; float centroid[V] = {}; float samples=0;
                                // Loop on 4×4 samples (TODO: vectorize, loop on coverage mask bitscan)
                                for(uint sampleI=0; sampleI<16; sampleI++) {
                                    float* const sample = 16*16*sampleI+pixel; //planar samples (free cache on mostly non-subsampled patterns)
                                    const vec2 sampleXY = XY[sampleI]; //TODO: latin square pattern

                                    vec3 XY1 = vec3(((binXY*4.f+blockXY)*4.f+pixelXY)*4.f+sampleXY+vec2(1.f/2, 1.f/2), 1.f);
                                    float w = 1/dot(iw,XY1);
                                    float z = w*dot(iz,XY1);

                                    float& depth = *sample;
                                    if(z>=depth) {
                                        depth = z;
                                        samples++;
                                        mask |= 1<<sampleI;
                                        for(int i=0;i<V;i++) centroid[i]+=w*dot(varyings[i],XY1);
                                    }
                                }

                                for(uint i=0;i<V;i++) centroid[i] /= samples;
                                vec4 bgra = vec4(0,0,0,1); //shader(face.faceAttributes,centroid);
                                float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                                // Occluded subsampled pixel become whole again
                                if(mask==(1<<16)-1) { //TODO: Hi-Z full pixel accept to avoid 16 Z-test + interpolation
                                    subsample[pixelPtr/16]&=~(1<<(pixelPtr%16));
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
                                    dstB=1; //DEBUG: coalesced pixel
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
                            continue;
                        }

                        // Scale from pixels to samples
                        for(int e=0;e<3;e++) pixelD[e] *= 4;

                        if(!(subsample[pixelPtr/16]&(1<<(pixelPtr%16)))) {
                            // Set subsampled pixel flag
                            subsample[pixelPtr/16]|=(1<<(pixelPtr%16));
                            // Broadcast pixel (first) sample to all (other) subsamples
                            for(uint sampleI=1;sampleI<16;sampleI++) {
                                float* const sample = 16*16*sampleI+pixel;
                                sample[0*64*64]=pixel[0*64*64];
                                sample[1*64*64]=pixel[1*64*64];
                                sample[2*64*64]=pixel[2*64*64];
                                sample[3*64*64]=pixel[3*64*64];
                            }
                        }

                        uint mask=0; float centroid[V] = {}; float samples=0;
                        // Loop on 4×4 samples (TODO: vectorize, loop on coverage mask bitscan)
                        for(uint sampleI=0; sampleI<16; sampleI++) {
                            float sampleD[3]; for(int e=0;e<3;e++) sampleD[e] = pixelD[e] + step[e][sampleI];

                            // reject
                            if((sampleD[0] + reject[0] <= 0) || (sampleD[1] + reject[1] <= 0) || (sampleD[2] + reject[2] <= 0) ) continue;

                            const vec2 sampleXY = XY[sampleI]; //TODO: latin square pattern
                            vec3 XY1 = vec3(((binXY*4.f+blockXY)*4.f+pixelXY)*4.f+sampleXY+vec2(1.f/2, 1.f/2), 1.f);
                            float w = 1/dot(iw,XY1);
                            float z = w*dot(iz,XY1);

                            float* const sample = 16*16*sampleI+pixel; //planar samples (free cache on mostly non-subsampled patterns)
                            float& depth = *sample;
                            if(z>=depth) {
                                depth = z;
                                samples++;
                                mask |= 1<<sampleI;
                                for(int i=0;i<V;i++) centroid[i]+=w*dot(varyings[i],XY1);
                            }
                        }

                        for(uint i=0;i<V;i++) centroid[i] /= samples;
                        vec4 bgra = vec4(0,0,0,1); //shader(face.faceAttributes,centroid);
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
            }
            bin.faceCount=0;
        }
        faceCount=0;
    }
};

