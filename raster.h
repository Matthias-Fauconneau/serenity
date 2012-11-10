#pragma once
/// \file raster.h 3D rasterizer (polygon, circle)
#include "matrix.h"
#include "function.h"

template<class FaceAttributes, int V> using Shader = functor<vec4(FaceAttributes,float[V])>;

static constexpr int pixelSize=2; // multisample antialiasing (TODO: 4)

static constexpr int quadSize=4*pixelSize; //TODO
static constexpr int blockSize = 4*quadSize; //TODO

// 64x64 pixels tile
static constexpr int tileSize = 2*blockSize;
struct Tile { // 8K triangles (virtual) + 64K framebuffer (actual)
    uint16 cleared=0;
    uint16 faceCount=0;
    uint16 faces[tileSize*tileSize-2]; // maximum virtual capacity
    vec3 color[tileSize*tileSize]; float depth[tileSize*tileSize]; //TODO: 4x4 blocks of 4x4 quads (=16x16 quads)
};

/// Tiled render target
struct RenderTarget {
    int tileWidth,tileHeight; //in tiles
    Tile* tiles;
    vec3 color; float depth;

    // Allocates all tiles and flags them to be cleared before first render
    RenderTarget(uint width, uint height, vec3 color=vec3(1,1,1), float depth=-0x1p16f)
        : tileWidth(align(tileSize,width*pixelSize)/tileSize),
          tileHeight(align(tileSize,height*pixelSize)/tileSize),
          tiles(allocate16<Tile>(tileWidth*tileHeight)),
          color(color), depth(depth){
        for(int i: range(tileWidth*tileHeight)) { Tile& tile = tiles[i]; tile.cleared=0; tile.faceCount=0; }
    }
    ~RenderTarget() { unallocate(tiles,tileWidth*tileHeight); }

    // Resolves internal MSAA linear framebuffer for sRGB display on the active X window
    void resolve(int2 position, int2 size);
};

template<class FaceAttributes /*per-face constant attributes*/, int V /*per-vertex varying attributes*/> struct RenderPass {
    // triangle face with 3F constant face attributes and V varying (perspective-interpolated) vertex attributes
    struct VertexAttributes { vec3 vertexAttributes[V]; };
    struct Face {
        mat3 M; vec3 Z;
        VertexAttributes vertexAttributes;
        FaceAttributes faceAttributes;
    };

    RenderTarget& target;
    uint16 faceCount=0;
    Face faces[4096]; // maximum virtual capacity
    RenderPass(RenderTarget& target):target(target){}

    // Implementation is inline to allow per-pass face attributes specialization

    // Submits triangles for tile binning, actual rendering is deferred until render
    void submit(vec4 A, vec4 B, vec4 C, VertexAttributes vertexAttributes, FaceAttributes faceAttributes) {
        assert(A.w==1); assert(B.w==1); assert(C.w==1);
        mat3 M = mat3(A.xyw(), B.xyw(), C.xyw());
        float det = M.det();
        if(det<=1) return; //small or back-facing triangle
        M = M.adjugate(); //elegant definition of edge equations
        faces[faceCount] = Face __(M, vec3(A.z,B.z,C.z), vertexAttributes, faceAttributes);

        int2 min = ::max(int2(0,0),int2(floor(::min(::min(A.xy(),B.xy()),C.xy())))/tileSize);
        int2 max = ::min(int2(target.tileWidth-1,target.tileHeight-1),int2(ceil(::max(::max(A.xy(),B.xy()),C.xy())))/tileSize);
        for(int tileY=min.y; tileY<=max.y; tileY++) for(int tileX=min.x; tileX<=max.x; tileX++) {
            Tile& tile = target.tiles[tileY*target.tileWidth+tileX];
            tile.faces[tile.faceCount++]=faceCount;
        }
        faceCount++;
        if(faceCount>sizeof(faces)/sizeof(Face)) error("Overflow");
    }

    // For each tile, rasterizes and shade all triangles using given shader
    void render(const Shader<FaceAttributes,V>& shader) {
        for(int tileI=0; tileI<target.tileHeight; tileI++) for(int tileJ=0; tileJ<target.tileWidth; tileJ++) {
            Tile& tile = target.tiles[tileI*target.tileWidth+tileJ];
            if(!tile.cleared) {
                clear(tile.color,tileSize*tileSize,target.color);
                clear(tile.depth,tileSize*tileSize,target.depth);
                tile.cleared=1;
            }
            int tileY=tileI*tileSize, tileX=tileJ*tileSize;
            for(int i=0; i<tile.faceCount; i++) {
                const Face& face = faces[tile.faces[i]];
                mat3 M = face.M;
                // Interpolation functions (-dy, dx, d)
                vec3 e0 = vec3(1,0,0)*M; if(e0.x>0/*dy<0*/ || (e0.x==0/*dy=0*/ && e0.y<0/*dx<0*/)) e0.z++;
                vec3 e1 = vec3(0,1,0)*M; if(e1.x>0/*dy<0*/ || (e1.x==0/*dy=0*/ && e1.y<0/*dx<0*/)) e1.z++;
                vec3 e2 = vec3(0,0,1)*M; if(e2.x>0/*dy<0*/ || (e2.x==0/*dy=0*/ && e2.y<0/*dx<0*/)) e2.z++;
                vec3 iz = face.Z*M;
                vec3 iw = vec3(1,1,1)*M;
                vec3 varyings[V];
                for(int i=0;i<V;i++) varyings[i] = face.vertexAttributes.vertexAttributes[i]*M;

                const int pixelPerTile = tileSize/pixelSize;
                for(int pixelI=0; pixelI<pixelPerTile; pixelI++) for(int pixelJ=0; pixelJ<pixelPerTile; pixelJ++) {
                    int pixelIndex = (pixelI*pixelPerTile+pixelJ)*pixelSize*pixelSize;
                    vec3* pixelColor = &tile.color[pixelIndex]; float* pixelDepth = &tile.depth[pixelIndex];
                    int pixelY = tileY+pixelI*pixelSize, pixelX=tileX+pixelJ*pixelSize;

                    //TODO: 4x4 16x16px blocks
                    uint mask=0, bit=1;
                    float centroid[V] = {}; float samples=0;

                    for(int sampleI=0; sampleI<pixelSize; sampleI++) for(int sampleJ=0; sampleJ<pixelSize; sampleJ++) {
                        int sampleY = pixelY+sampleI, sampleX = pixelX+sampleJ;
                        vec3 XY1(sampleX+1.f/2, sampleY+1.f/2, 1);

                        float d0 = dot(e0,XY1), d1 = dot(e1,XY1), d2 = dot(e2,XY1);
                        if(d0>0 && d1>0 && d2>0) {
                            float w = 1/dot(iw,XY1);
                            float z = w*dot(iz,XY1);

                            float& depth = pixelDepth[sampleI*pixelSize+sampleJ];
                            if(z>=depth) {
                                depth = z;
                                samples++;
                                mask |= bit;
                                for(int i=0;i<V;i++) centroid[i]+=w*dot(varyings[i],XY1);
                            }
                        }
                        bit <<= 1;
                    }
                    if(mask) {
                        for(int i=0;i<V;i++) centroid[i] /= samples;
                        vec4 color_opacity = shader(face.faceAttributes,centroid);
                        float a = color_opacity.w; vec3 color = color_opacity.xyz()*a;
                        //TODO: z/color compression
                        uint bit=1;
                        for(int i=0;i<pixelSize;i++) for(int j=0;j<pixelSize;j++) {
                            if(mask&bit) {
                                vec3& s = pixelColor[i*pixelSize+j];
                                s=s*(1-a)+color;
                            }
                            bit <<= 1;
                        }
                    }
                }
            }
            tile.faceCount=0;
        }
        faceCount=0;
    }
};

