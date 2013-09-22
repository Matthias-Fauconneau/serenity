#pragma once
#include "vector.h"

/*struct xbspVertex {
    vec3 position;
    vec2 texture;
    vec2 lightmap;
    vec3 normal;
    float color[4];
    float lightColor[4];
    vec3 lightDirection;
};*/

struct ibspVertex {
    vec3 position;
    vec2 texture;
    vec2 lightmap;
    vec3 normal;
    uint8 color[4];
};

struct bspShader {
    char name[64];
    int surfaceFlags;
    int contentFlags;
};

struct bspFace {
    int texture; // Texture index.
    int effect;  // Index into lump 12 (Effects), or -1.
    int type;    // Face type. 1=polygon, 2=patch, 3=mesh, 4=billboard
    int firstVertex;  // Index of first vertex.
    int numVertexes; // Number of vertices.
    int firstIndex; // Index of first mesh index.
    int numIndices;  // Number of indices.
    int lightMapIndex; // Lightmap index.
    int lightMapStart[2]; // Corner of this face's lightmap image in lightmap.
    int lightMapSize[2]; // Size of this face's lightmap image in lightmap.
    float lightMapOrigin[3]; // World space origin of lightmap.
    float lm_vecs[2][3]; //  World space lightmap s and t unit vectors.
    float normal[3]; // Surface normal.
    int size[2]; // Patch dimensions.
};

struct bspLeaf {
    int cluster;
    int area;
    int boundingBox[6];
    int firstFace;
    int numFaces;
    int firstBrush;
    int nofBrushes;
};

struct bspModel {
    float boundingBox[6];
    int firstFace;
    int numFaces;
    int firstBrush;
    int numBrushes;
};

struct BSP {
    char magic[4]; // "IBSP" (or "XBSP" for XReaL)
    int version; // ET:47 XReaL:48
#define LUMP(T,s,p) \
    struct { int offset, length; } s##Lump; \
    inline ref<T> p() const { return ref<T>((const T*)((const char*)this+s##Lump.offset), s##Lump.length / sizeof(T)); }
    LUMP( char, entity, entities )  // Game-related object descriptions.
    LUMP( bspShader, shader, shaders )  // Surface descriptions.
    LUMP( vec4, plane, planes )    // Planes used by map geometry.
    LUMP( char, node, nodes )     // BSP tree nodes.
    LUMP( bspLeaf, leaf, leaves )     // BSP tree leaves.
    LUMP( int, leafFace, leafFaces ) // Lists of face indices, one list per leaf.
    LUMP( int, leafBrush, leafBrushes)// Lists of brush indices, one list per leaf.
    LUMP( bspModel, model, models ) // Descriptions of rigid world geometry in map.
    LUMP( char, brush, brushes )   // Convex polyhedra used to describe solid space.
    LUMP( char, brushSide, brushSides )// Brush surfaces.
    LUMP( ibspVertex, vertex, vertices )  // Vertices used to describe faces. //FIXME: or xbspVertex
    LUMP( int, index, indices ) // Lists of indices
    LUMP( char, effect, effects )   // List of special map effects.
    LUMP( bspFace, face, faces )     // Surface geometry.
    LUMP( char, lightMap, lightMaps ) // Packed lightmap data.
    LUMP( char, lightVoxel, lightVolume ) // Local illumination data.
    LUMP( char, visData, visData )   // Cluster-cluster visibility data.
#undef LUMP
};
