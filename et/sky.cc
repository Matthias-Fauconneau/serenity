#include "thread.h"
#include "string.h"
#include "math.h"
#include "vector.h"

#define Square(x) ((x) * (x))

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];
#define DotProduct(x, y)         ((x)[0] * (y)[0] + (x)[1] * (y)[1] + (x)[2] * (y)[2])
#define VectorSubtract(a, b, c)   ((c)[0] = (a)[0] - (b)[0], (c)[1] = (a)[1] - (b)[1], (c)[2] = (a)[2] - (b)[2])
#define VectorAdd(a, b, c)        ((c)[0] = (a)[0] + (b)[0], (c)[1] = (a)[1] + (b)[1], (c)[2] = (a)[2] + (b)[2])
#define VectorCopy(a, b)         ((b)[0] = (a)[0], (b)[1] = (a)[1], (b)[2] = (a)[2])
#define VectorScale(v, s, o)    ((o)[0] = (v)[0] * (s), (o)[1] = (v)[1] * (s), (o)[2] = (v)[2] * (s))
#define VectorMA(v, s, b, o)    ((o)[0] = (v)[0] + (b)[0] * (s), (o)[1] = (v)[1] + (b)[1] * (s), (o)[2] = (v)[2] + (b)[2] * (s))
void VectorNormalize(vec3_t& v) { vec3 V=normalize(vec3(v[0],v[1],v[2])); v[0]=V.x, v[1]=V.y, v[2]=V.z; }
vec3_t vec3_origin;
struct shaderCommands_t {
    uint indexes[10000];
    vec4_t xyz[10000];
    vec2 texCoords0[10000];

    uint numIndexes = 0;
    uint numVertexes = 0;
} tess;
#define SKY_SUBDIVISIONS        8
#define HALF_SKY_SUBDIVISIONS   (SKY_SUBDIVISIONS / 2)

static float s_cloudTexCoords[6][SKY_SUBDIVISIONS + 1][SKY_SUBDIVISIONS + 1][2];
static float s_cloudTexP[6][SKY_SUBDIVISIONS + 1][SKY_SUBDIVISIONS + 1];

static vec3_t s_skyPoints[SKY_SUBDIVISIONS + 1][SKY_SUBDIVISIONS + 1];
static float  s_skyTexCoords[SKY_SUBDIVISIONS + 1][SKY_SUBDIVISIONS + 1][2];

float sky_mins[2][6], sky_maxs[2][6];
float sky_min = 0, sky_max = 1;

vec4_t origin;

static void MakeSkyVec(float s, float t, int axis, float outSt[2], vec3_t outXYZ) {
    float boxSize = 1024 / sqrt(3.);
    vec3_t b;
    b[0] = s * boxSize;
    b[1] = t * boxSize;
    b[2] = boxSize;

    for(int j = 0 ; j < 3 ; j++) {
        static int st_to_vec[6][3] =     // 1 = s, 2 = t, 3 = cloud?
        {
            { 3,  -1, 2 },
            { -3, 1,  2 },

            { 1,  3,  2 },
            { -1, -3, 2 },

            { -2, -1, 3 }, // 0 degrees yaw, look straight up
            { 2,  -1, -3}   // look straight down
        };
        int k = st_to_vec[axis][j];
        if (k < 0) outXYZ[j] = -b[-k - 1];
        else outXYZ[j] = b[k - 1];
    }

    // avoid bilerp seam
    s = (s + 1) * 0.5;
    t = (t + 1) * 0.5;
    if (s < sky_min) s = sky_min;
    else if (s > sky_max) s = sky_max;
    if (t < sky_min) t = sky_min;
    else if (t > sky_max) t = sky_max;
    t = 1.0 - t;
    if (outSt) {
        outSt[0] = s;
        outSt[1] = t;
    }
}

static void AddSkyPolygon(int nump, vec3_t vecs) {
    // decide which face it maps to
    vec3_t v; VectorCopy(vec3_origin, v);
    {float* vp = vecs; for(int i = 0; i < nump ; i++, vp += 3) VectorAdd(vp, v, v);}
    vec3_t av;
    av[0] = abs(v[0]);
    av[1] = abs(v[1]);
    av[2] = abs(v[2]);
    int axis;
    if(av[0] > av[1] && av[0] > av[2]) {
        if (v[0] < 0) axis = 1;
        else axis = 0;
    }
    else if (av[1] > av[2] && av[1] > av[0]) {
        if (v[1] < 0) axis = 3;
        else axis = 2;
    }
    else {
        if (v[2] < 0) axis = 5;
        else axis = 4;
    }

    // project new texture coords
    for(int i = 0 ; i < nump ; i++, vecs += 3) {
        static int vec_to_st[6][3] = {
            { -2, 3,  1  },
            { 2,  3,  -1 },

            { 1,  3,  2  },
            { -1, 3,  -2 },

            { -2, -1, 3  },
            { -2, 1,  -3 }
        };

        int j = vec_to_st[axis][2];
        float dv;
        if (j > 0) dv = vecs[j - 1];
        else dv = -vecs[-j - 1];
        if (dv < 0.001) continue;   // don't divide by zero
        j = vec_to_st[axis][0];
        float s;
        if (j < 0) s = -vecs[-j - 1] / dv;
        else s = vecs[j - 1] / dv;
        j = vec_to_st[axis][1];
        float t;
        if (j < 0) t = -vecs[-j - 1] / dv;
        else t = vecs[j - 1] / dv;

        if (s < sky_mins[0][axis]) sky_mins[0][axis] = s;
        if (t < sky_mins[1][axis]) sky_mins[1][axis] = t;
        if (s > sky_maxs[0][axis]) sky_maxs[0][axis] = s;
        if (t > sky_maxs[1][axis]) sky_maxs[1][axis] = t;
    }
}

static void ClipSkyPolygon(int nump, vec3_t vecs, int stage) {

    const float ON_EPSILON = 0.1f;            // point on plane side epsilon
    const int MAX_CLIP_VERTS = 64;
    enum { SIDE_FRONT, SIDE_BACK, SIDE_ON };

    if (nump > MAX_CLIP_VERTS - 2) error("ClipSkyPolygon: MAX_CLIP_VERTS");
    if (stage == 6) { // fully clipped, so draw it
        AddSkyPolygon(nump, vecs);
        return;
    }

    float    dists[MAX_CLIP_VERTS];
    int      sides[MAX_CLIP_VERTS];

    bool front = false, back = false;
    static vec3_t sky_clip[6] = {{ 1,  1,  0 }, { 1,  -1, 0 }, { 0,  -1, 1 }, { 0,  1,  1 }, { 1,  0,  1 }, { -1, 0,  1 }};
    float* norm = sky_clip[stage];
    {float* v = vecs; for(int i = 0 ; i < nump ; i++, v += 3) {
        float d = DotProduct(v, norm);
        if (d > ON_EPSILON) {
            front    = true;
            sides[i] = SIDE_FRONT;
        }
        else if (d < -ON_EPSILON) {
            back     = true;
            sides[i] = SIDE_BACK;
        } else {
            sides[i] = SIDE_ON;
        }
        dists[i] = d;
    }}

    if (!front || !back) { // not clipped
        ClipSkyPolygon(nump, vecs, stage + 1);
        return;
    }

    // clip it
    sides[nump] = sides[0];
    dists[nump] = dists[0];
    VectorCopy(vecs, (vecs + (nump * 3)));
    vec3_t   newv[2][MAX_CLIP_VERTS];
    int      newc[2] = {0,0};

    {float* v = vecs; for(int i = 0; i < nump ; i++, v += 3) {
        /**/ if(sides[i]==SIDE_FRONT) {
            VectorCopy(v, newv[0][newc[0]]);
            newc[0]++;
        }
        else if(sides[i]==SIDE_BACK) {
            VectorCopy(v, newv[1][newc[1]]);
            newc[1]++;
        }
        else if(sides[i]==SIDE_ON) {
            VectorCopy(v, newv[0][newc[0]]);
            newc[0]++;
            VectorCopy(v, newv[1][newc[1]]);
            newc[1]++;
        }

        if (sides[i] == SIDE_ON || sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i]) continue;

        float d = dists[i] / (dists[i] - dists[i + 1]);
        for(int j = 0 ; j < 3 ; j++) {
            float e                   = v[j] + d * (v[j + 3] - v[j]);
            newv[0][newc[0]][j] = e;
            newv[1][newc[1]][j] = e;
        }
        newc[0]++;
        newc[1]++;
    }}

    // continue
    ClipSkyPolygon(newc[0], newv[0][0], stage + 1);
    ClipSkyPolygon(newc[1], newv[1][0], stage + 1);
}

void drawSky(float cloudHeight) {
    for(int i = 0; i < 6; i++) {
        for(int t = 0; t <= SKY_SUBDIVISIONS; t++) {
            for(int s = 0; s <= SKY_SUBDIVISIONS; s++) {
                float  radiusWorld = 4096;
                float  p;
                float  sRad, tRad;
                vec3_t skyVec;
                vec3_t v;

                // compute vector from view origin to sky side integral point
                MakeSkyVec((s - HALF_SKY_SUBDIVISIONS) / ( float ) HALF_SKY_SUBDIVISIONS,
                           (t - HALF_SKY_SUBDIVISIONS) / ( float ) HALF_SKY_SUBDIVISIONS,
                           i,
                           NULL,
                           skyVec);

                // compute parametric value 'p' that intersects with cloud layer
                p = (1.0f / (2 * DotProduct(skyVec, skyVec))) *
                    (-2 * skyVec[2] * radiusWorld +
                     2 * sqrt(Square(skyVec[2]) * Square(radiusWorld) +
                              2 * Square(skyVec[0]) * radiusWorld * cloudHeight +
                              Square(skyVec[0]) * Square(cloudHeight) +
                              2 * Square(skyVec[1]) * radiusWorld * cloudHeight +
                              Square(skyVec[1]) * Square(cloudHeight) +
                              2 * Square(skyVec[2]) * radiusWorld * cloudHeight +
                              Square(skyVec[2]) * Square(cloudHeight)));

                s_cloudTexP[i][t][s] = p;

                // compute intersection point based on p
                VectorScale(skyVec, p, v);
                v[2] += radiusWorld;

                // compute vector from world origin to intersection point 'v'
                VectorNormalize(v);

                sRad = acos(v[0]);
                tRad = acos(v[1]);

                s_cloudTexCoords[i][t][s][0] = sRad;
                s_cloudTexCoords[i][t][s][1] = tRad;
            }
        }
    }

    {
        vec3_t p[5];        // need one extra point for clipping
        for (int i = 0 ; i < 6 ; i++) { sky_mins[0][i] = sky_mins[1][i] = 9999; sky_maxs[0][i] = sky_maxs[1][i] = -9999; }
        for(uint i = 0; i < tess.numIndexes; i += 3) {
            for(int j = 0 ; j < 3 ; j++) VectorSubtract(tess.xyz[tess.indexes[i + j]], origin, p[j]);
            ClipSkyPolygon(3, p[0], 0);
        }
    }

    // generate the vertexes for all the clouds, which will be drawn by the generic shader routine
    tess.numIndexes  = 0;
    tess.numVertexes = 0;
    int   mins[2], maxs[2];

    for(int i = 0; i < 5; i++) {
        float MIN_T = i==4 ? -HALF_SKY_SUBDIVISIONS : -1;

        sky_mins[0][i] = floor(sky_mins[0][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
        sky_mins[1][i] = floor(sky_mins[1][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
        sky_maxs[0][i] = ceil(sky_maxs[0][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
        sky_maxs[1][i] = ceil(sky_maxs[1][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;

        if ((sky_mins[0][i] >= sky_maxs[0][i]) ||
            (sky_mins[1][i] >= sky_maxs[1][i]))
        {
            continue;
        }

        mins[0] = (int)(sky_mins[0][i] * HALF_SKY_SUBDIVISIONS);
        mins[1] = (int)(sky_mins[1][i] * HALF_SKY_SUBDIVISIONS);
        maxs[0] = (int)(sky_maxs[0][i] * HALF_SKY_SUBDIVISIONS);
        maxs[1] = (int)(sky_maxs[1][i] * HALF_SKY_SUBDIVISIONS);

        if (mins[0] < -HALF_SKY_SUBDIVISIONS)
        {
            mins[0] = -HALF_SKY_SUBDIVISIONS;
        }
        else if (mins[0] > HALF_SKY_SUBDIVISIONS)
        {
            mins[0] = HALF_SKY_SUBDIVISIONS;
        }
        if (mins[1] < MIN_T)
        {
            mins[1] = MIN_T;
        }
        else if (mins[1] > HALF_SKY_SUBDIVISIONS)
        {
            mins[1] = HALF_SKY_SUBDIVISIONS;
        }

        if (maxs[0] < -HALF_SKY_SUBDIVISIONS)
        {
            maxs[0] = -HALF_SKY_SUBDIVISIONS;
        }
        else if (maxs[0] > HALF_SKY_SUBDIVISIONS)
        {
            maxs[0] = HALF_SKY_SUBDIVISIONS;
        }
        if (maxs[1] < MIN_T)
        {
            maxs[1] = MIN_T;
        }
        else if (maxs[1] > HALF_SKY_SUBDIVISIONS)
        {
            maxs[1] = HALF_SKY_SUBDIVISIONS;
        }

        // iterate through the subdivisions
        for(int t = mins[1] + HALF_SKY_SUBDIVISIONS; t <= maxs[1] + HALF_SKY_SUBDIVISIONS; t++) {
            for(int s = mins[0] + HALF_SKY_SUBDIVISIONS; s <= maxs[0] + HALF_SKY_SUBDIVISIONS; s++) {
                MakeSkyVec((s - HALF_SKY_SUBDIVISIONS) / ( float ) HALF_SKY_SUBDIVISIONS,
                           (t - HALF_SKY_SUBDIVISIONS) / ( float ) HALF_SKY_SUBDIVISIONS,
                           i,
                           NULL,
                           s_skyPoints[t][s]);

                s_skyTexCoords[t][s][0] = s_cloudTexCoords[i][t][s][0];
                s_skyTexCoords[t][s][1] = s_cloudTexCoords[i][t][s][1];
            }
        }

        int vertexStart = tess.numVertexes;
        int tHeight     = maxs[1] - mins[1] + 1;
        int sWidth      = maxs[0] - mins[0] + 1;

        for(int t = mins[1] + HALF_SKY_SUBDIVISIONS; t <= maxs[1] + HALF_SKY_SUBDIVISIONS; t++) {
            for(int s = mins[0] + HALF_SKY_SUBDIVISIONS; s <= maxs[0] + HALF_SKY_SUBDIVISIONS; s++) {
                VectorAdd(s_skyPoints[t][s], origin, tess.xyz[tess.numVertexes]);
                //log(tess.xyz[tess.numVertexes]);
                tess.texCoords0[tess.numVertexes][0] = s_skyTexCoords[t][s][0];
                tess.texCoords0[tess.numVertexes][1] = s_skyTexCoords[t][s][1];

                tess.numVertexes++;
            }
        }

        // only add indexes for one pass, otherwise it would draw multiple times for each pass
        for(int t = 0; t < tHeight - 1; t++) {
            for(int s = 0; s < sWidth - 1; s++) {
                tess.indexes[tess.numIndexes] = vertexStart + s + t * (sWidth);
                tess.numIndexes++;
                tess.indexes[tess.numIndexes] = vertexStart + s + (t + 1) * (sWidth);
                tess.numIndexes++;
                tess.indexes[tess.numIndexes] = vertexStart + s + 1 + t * (sWidth);
                tess.numIndexes++;

                tess.indexes[tess.numIndexes] = vertexStart + s + (t + 1) * (sWidth);
                tess.numIndexes++;
                tess.indexes[tess.numIndexes] = vertexStart + s + 1 + (t + 1) * (sWidth);
                tess.numIndexes++;
                tess.indexes[tess.numIndexes] = vertexStart + s + 1 + t * (sWidth);
                tess.numIndexes++;
            }
        }
    }
}
