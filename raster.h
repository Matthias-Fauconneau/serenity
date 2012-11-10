#pragma once
/// \file raster.h 3D rasterizer (polygon, circle)
#include "matrix.h"
#include "function.h"

template<class FaceAttributes, int V> using Shader = functor<vec4(FaceAttributes,float[V])>;

/// 64×64 pixels bin for L1 cache locality (64×64×RGBZ×float~64KB)
struct Bin { // 8K triangles (virtual) + 64K framebuffer (actual)
    uint16 cleared=0;
    uint16 faceCount=0;
    uint16 faces[64*64-2]; // maximum virtual capacity
    float depth[64*64], blue[64*64], green[64*64], red[64*64];
};

/// Tiled render target
struct RenderTarget {
    int width,height; //in bins
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

template<class FaceAttributes /*per-face constant attributes*/, int V /*per-vertex varying attributes*/> struct RenderPass {
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
    Face faces[4096]; // maximum virtual capacity
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
        for(int binY=min.y; binY<=max.y; binY++) for(int binX=min.x; binX<=max.x; binX++) {
            Bin& bin = target.bins[binY*target.width+binX];
            bin.faces[bin.faceCount++]=faceCount;
            if(faceCount>sizeof(bin.faces)/sizeof(uint16)) error("Overflow");
        }
        faceCount++;
        if(faceCount>sizeof(faces)/sizeof(Face)) error("Overflow");
    }

    // For each bin, rasterizes and shade all triangles using given shader
    void render(const Shader<FaceAttributes,V>& shader) {
        // Loop on all bins (64x64 samples (16x16 pixels))
        for(int binI=0; binI<target.height; binI++) for(int binJ=0; binJ<target.width; binJ++) {
            Bin& bin = target.bins[binI*target.width+binJ];
            if(!bin.cleared) {
                clear(bin.depth,64*64,target.depth);
                clear(bin.blue,64*64,target.blue);
                clear(bin.green,64*64,target.green);
                clear(bin.red,64*64,target.red);
                bin.cleared=1;
            }
            float* const buffer = bin.depth;
            const vec2 binXY = vec2(binJ,binI);
            // Loop on all faces in the bin
            for(int faceI=0; faceI<bin.faceCount; faceI++) {
                const Face& face = faces[bin.faces[faceI]];
                const mat3& E = face.E;

                // Interpolation functions (-dy, dx, d)
                const vec2 e[3] = {E[0].xy(), E[1].xy(), E[2].xy()};

                // Initial distance step
                const float pixelToBin = 1.f/64;
                float binD0 = pixelToBin*E[0].z + dot(e[0],binXY),
                        binD1 = pixelToBin*E[1].z + dot(e[1],binXY),
                        binD2 = pixelToBin*E[2].z + dot(e[2],binXY);

                // trivial accept/reject corner for each edge
                vec2 accept[3], reject[3];
                for(int edgeI=0; edgeI<3; edgeI++) {
                    vec2 edge = e[edgeI];
                    reject[edgeI] = vec2(edge.x>0,edge.y>0);
                    accept[edgeI] = vec2(!(edge.x>0),!(edge.y>0));
                }

                // trivial reject
                if(binD0 + dot(e[0],reject[0]) <= 0) continue;
                if(binD1 + dot(e[1],reject[1]) <= 0) continue;
                if(binD2 + dot(e[2],reject[2]) <= 0) continue;

                const vec3 iw = E[0]+E[1]+E[2];
                const vec3 iz = E*face.Z;
                vec3 varyings[V]; for(int i=0;i<V;i++) varyings[i] = E*face.vertexAttributes.data[i];

                //4×4 xy steps constant mask
                static constexpr vec2 XY[4*4] = {
                                                  vec2(0,0),vec2(1,0),vec2(2,0),vec2(3,0),
                                                  vec2(0,1),vec2(1,1),vec2(2,1),vec2(3,1),
                                                  vec2(0,2),vec2(1,2),vec2(2,2),vec2(3,2),
                                                  vec2(0,3),vec2(1,3),vec2(2,3),vec2(3,3),
                                                };

                // Scale from bin to blocks
                binD0 *= 4, binD1 *= 4, binD2 *= 4;

                // Loop on 4×4 blocks (TODO: vectorize, loop on coverage mask bitscan)
                for(int blockI=0; blockI<4*4; blockI++) {
                    float* const block = buffer + blockI*(4*4)*(4*4);
                    const vec2 blockXY = XY[blockI];
                    float blockD0 = binD0 + dot(e[0],blockXY),
                            blockD1 = binD1 + dot(e[1],blockXY),
                            blockD2 = binD2 + dot(e[2],blockXY);

                    // trivial reject
                    if(blockD0 + dot(e[0],reject[0]) <= 0) continue;
                    if(blockD1 + dot(e[1],reject[1]) <= 0) continue;
                    if(blockD2 + dot(e[2],reject[2]) <= 0) continue;

                    // TODO: block Hi-Z

                    // Scale from blocks to pixels
                    blockD0 *= 4, blockD1 *= 4, blockD2 *= 4;

                    // Loop on 4×4 pixels (TODO: vectorize, loop on coverage mask bitscan)
                    for(int pixelI=0; pixelI<4*4; pixelI++) {
                        float* const pixel = block+pixelI*16;
                        const vec2 pixelXY = XY[pixelI];
                        float pixelD0 = blockD0 + dot(e[0],pixelXY),
                                pixelD1 = blockD1 + dot(e[1],pixelXY),
                                pixelD2 = blockD2 + dot(e[2],pixelXY);

                        // trivial reject
                        if(pixelD0 + dot(e[0],reject[0]) <= 0) continue;
                        if(pixelD1 + dot(e[1],reject[1]) <= 0) continue;
                        if(pixelD2 + dot(e[2],reject[2]) <= 0) continue;
                        // TODO: interleaved pixels (planar samples) + flag subsampled

                        // Scale from pixels to samples
                        pixelD0 *= 4, pixelD1 *= 4, pixelD2 *= 4;

                        uint mask=0; float centroid[V] = {}; float samples=0;
                        // Loop on 4×4 samples (TODO: vectorize, loop on coverage mask bitscan)
                        for(int sampleI=0; sampleI<16; sampleI++) {
                            float* const sample = pixel+sampleI;
                            const vec2 sampleXY = XY[sampleI]; //TODO: latin square pattern
                            const float
                                    sampleD0 = pixelD0 + dot(e[0],sampleXY),
                                    sampleD1 = pixelD1 + dot(e[1],sampleXY),
                                    sampleD2 = pixelD2 + dot(e[2],sampleXY);

                            if(sampleD0>0 && sampleD1>0 && sampleD2>0) {
                                vec3 XY1 = vec3( ((binXY*4.f+blockXY)*4.f+pixelXY)*4.f+sampleXY+vec2(1.f/2, 1.f/2), 1.f);
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
                        }

                        for(int i=0;i<V;i++) centroid[i] /= samples;
                        vec4 bgra = shader(face.faceAttributes,centroid);
                        float srcB=bgra.x, srcG=bgra.y, srcR=bgra.z, srcA = bgra.w;
                        for(int sampleI=0;sampleI<16;sampleI++) {
                            float* const sample = pixel+sampleI;
                            if(mask&(1<<sampleI)) {
                                float& dstB = sample[1*64*64];
                                float& dstG = sample[2*64*64];
                                float& dstR = sample[3*64*64];
                                dstB=(1-srcA)*dstB+srcA*srcB;
                                dstG=(1-srcA)*dstG+srcA*srcG;
                                dstR=(1-srcA)*dstR+srcA*srcR;
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

