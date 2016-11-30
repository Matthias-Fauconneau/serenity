#pragma once
#include "matrix.h"
#include "parallel.h"
#include "mwc.h"
#include "time.h"
#include "sphere.h"

inline mat4 shearedPerspective(const float s, const float t, const float near, const float far) { // Sheared perspective (rectification)
    const float left = (-1-s), right = (1-s);
    const float bottom = (-1-t), top = (1-t);
    mat4 M;
    M(0,0) = 2*near / (right-left);
    M(1,1) = 2*near / (top-bottom);
    M(0,2) = (right+left) / (right-left);
    M(1,2) = (top+bottom) / (top-bottom);
    M(2,2) = - (far+near) / (far-near);
    M(2,3) = - 2*far*near / (far-near);
    M(3,2) = - 1;
    M(3,3) = 0;
    M.scale(vec3(1,1,-1)); // Z-
    M.translate(vec3(-s,-t,0));
    return M;
}

#include "raster.h"

inline void cross(const v8sf Ax, const v8sf Ay, const v8sf Az, const v8sf Bx, const v8sf By, const v8sf Bz, v8sf& X, v8sf& Y, v8sf& Z) {
    X = Ay*Bz - By*Az;
    Y = Az*Bx - Bz*Ax;
    Z = Ax*By - Bx*Ay;
}

inline v8sf dot(const v8sf Ax, const v8sf Ay, const v8sf Az, const v8sf Bx, const v8sf By, const v8sf Bz) {
    return Ax*Bx + Ay*By + Az*Bz;
}

// "Fast, Minimum Storage Ray/Triangle Intersection"
static inline v8sf intersect(const v8sf xA, const v8sf yA, const v8sf zA,
                             const v8sf xB, const v8sf yB, const v8sf zB,
                             const v8sf xC, const v8sf yC, const v8sf zC,
                             const v8sf  Ox, const v8sf  Oy, const v8sf Oz,
                             const v8sf  Dx, const v8sf  Dy, const v8sf Dz,
                             v8sf& det, v8sf& u, v8sf& v) {
    const v8sf eACx = xC - xA;
    const v8sf eACy = yC - yA;
    const v8sf eACz = zC - zA;
    v8sf Px, Py, Pz; cross(Dx, Dy, Dz, eACx, eACy, eACz, Px, Py, Pz);
    const v8sf Tx = Ox - xA;
    const v8sf Ty = Oy - yA;
    const v8sf Tz = Oz - zA;
    u = dot(Tx, Ty, Tz, Px, Py, Pz);
    const v8sf eABx = xB - xA;
    const v8sf eABy = yB - yA;
    const v8sf eABz = zB - zA;
    det = dot(eABx, eABy, eABz, Px, Py, Pz);
    v8sf Qx, Qy, Qz; cross(Tx, Ty, Tz, eABx, eABy, eABz, Qx, Qy, Qz);
    v = dot(Dx, Dy, Dz, Qx, Qy, Qz);
    const v8sf t = dot(eACx, eACy, eACz, Qx, Qy, Qz) / det;
    return blend(float8(inff), t, det > 0 && u >= 0 && v >= 0 && u + v <= det && t > 0);
}

inline v8sf hmin(const v8sf x) {
    const v8sf v0 = __builtin_ia32_minps256(x, _mm256_alignr_epi8(x, x, 4));
    const v8sf v1 = __builtin_ia32_minps256(v0, _mm256_alignr_epi8(v0, v0, 8));
    return __builtin_ia32_minps256(v1, _mm256_permute2x128_si256(v1, v1, 0x01));
}

inline uint indexOfEqual(const v8sf x, const v8sf y) {
    return __builtin_ctz(::mask(x == y));
}


static inline vec3 refract(const float r, const vec3 N, const vec3 D) {
    const float c = -dot(N, D);
    return r*D + (r*c - sqrt(1-sq(r)*(1-sq(c))))*N;
}

// Uniformly distributed points on sphere (Marsaglia)
static inline Vec<v8sf, 3> sphere(Random& random) {
    v8sf t0, t1, sq;
    do {
        t0 = random()*2-1;
        t1 = random()*2-1;
        sq = t0*t0 + t1*t1;
    } while(mask(sq >= 1)); // FIXME: TODO: Partial accepts
    const v8sf r = sqrt(1-sq);
    return {{2*t0*r, 2*t1*r, 1-2*sq}};
}

// Uniformly distributed points on hemisphere directed towards N
static inline Vec<v8sf, 3> hemisphere(Random& random, vec3 N) {
    const Vec<v8sf, 3> S = sphere(random);
    const v8si negate = (N.x*S._[0] + N.y*S._[1] + N.z*S._[2]) < 0;
    return {{blend(S._[0], -S._[0], negate),
             blend(S._[1], -S._[1], negate),
             blend(S._[2], -S._[2], negate)}};
}

static inline Vec<v8sf, 3> cosine(Random& random) {
    const v8sf ξ1 = random();
    const v8sf ξ2 = random();
    const v8sf cosθ = sqrt(1-ξ1);
    const v8sf sinθ = sqrt(ξ1);
    const v8sf φ = 2*PI*ξ2;
    const Vec<v8sf, 2> cossinφ = cossin(φ);
    return {{sinθ * cossinφ._[0], sinθ * cossinφ._[1], cosθ}};
}

static inline uint popcount(v4uq m) { return __builtin_popcountll(m[0])+__builtin_popcountll(m[1])+__builtin_popcountll(m[2])+__builtin_popcountll(m[3]); }

struct Lookup {
    typedef v4uq mask;
    static constexpr uint S = sizeof(mask)*8;
    static constexpr uint N = 128;
    buffer<mask> lookup {N*N}; // 512K

    inline v8si index(const v8sf X, const v8sf Y, const v8sf Z) const {
        const Vec<v8sf, 2> UV = square(X, Y, Z);
        const v8si uIndex = cvtt((UV._[0]+1)*((N-1)/2.f));
        const v8si vIndex = cvtt((UV._[1]+1)*((N-1)/2.f));
        return vIndex*N+uIndex;
    }

    void generate(Random& random) {
        // FIXME: stratified
        v8sf X[S/8], Y[S/8], Z[S/8];
        for(uint i: range(S/8)) {
            Vec<v8sf, 3> s8 = cosine(random);
            X[i] = s8._[0];
            Y[i] = s8._[1];
            Z[i] = s8._[2];
        }
        static constexpr v8sf seq {0,1,2,3,4,5,6,7};
        static constexpr float scale = 1.f/((N-1)/2.f);
        static const v8sf seqF = scale*seq-1;
        for(uint vIndex: range(N)) {
            const v8sf v = scale*vIndex-1;
            const mask* line = lookup.begin()+vIndex*N;
            for(uint uIndex=0; uIndex<N; uIndex+=8) {
                const mask* span8 = line+uIndex;
                const Vec<v8sf, 3> XYZ = sphere(scale*uIndex+seqF, v);
                for(uint k: range(8)) {
                    const float x = XYZ._[0][k];
                    const float y = XYZ._[1][k];
                    const float z = XYZ._[2][k];
                    uint8* mask32 = (uint8*)(span8+k);
                    for(const uint i: range(S/8)) mask32[i] = ::mask((x*X[i] + y*Y[i] + z*Z[i]) >= 0);
                }
            }
        }
    }

    inline mask operator()(const vec3 n) const {
        const uint index = this->index(n.x, n.y, n.z)[0];
        return lookup[index];
    }
};

#define HALF 1 // FIXME: Accumulate singles, sample halfs
#if HALF
typedef half Float;
#else
typedef float Float;
#endif

struct Scene {
    struct Face {
        // Vertex attributes
        float u[3];
        float v[3];
        vec3 T[3], B[3], N[3];
        // Face attributes
        float reflect, refract, gloss; // Render (RaycastShader)
        const Float* BGR; uint2 size; // Display (TextureShader)
    };
    buffer<float> X[3], Y[3], Z[3]; // Quadrilaterals vertices world space positions XYZ coordinates
    buffer<float> emittanceB, emittanceG, emittanceR; // Face color attributes
    buffer<float> reflectanceB, reflectanceG, reflectanceR; // 1/PI
    buffer<Face> faces;
    array<uint> lights; // Face index of lights
    array<float> area; // Area of lights (sample proportionnal to area) (Divided by sum)
    array<float> CAF; // Cumulative area of lights (sample proportionnal to area)

    vec3 min, max;
    float scale, near, far;

#if RT
    static constexpr uint L = 1;
#else
    static constexpr uint L = 16;
#endif
    buffer<Lookup> lookups {L};
    Scene() { lookups.clear(); }
    uint count = 0;
    bool rasterize = true;
    bool specular = false, indirect = false;

    void fit() {
        // Fits scene
        min = inff, max = -inff;
        for(size_t i: range(3)) {
            min = ::min(min, vec3(::min(X[i]), ::min(Y[i]), ::min(Z[i])));
            max = ::max(max, vec3(::max(X[i]), ::max(Y[i]), ::max(Z[i])));
        }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        scale = 2./::max(max.x-min.x, max.y-min.y);
        near = scale*min.z;
        far = scale*max.z;
    }

    inline size_t raycast(vec3 O, vec3 d) const {
        assert(faces.size < faces.capacity && align(8, faces.size)==faces.capacity);
        float value = inff; size_t index = faces.size;
        const v8sf Ox = float8(O.x);
        const v8sf Oy = float8(O.y);
        const v8sf Oz = float8(O.z);
        const v8sf dx = float8(d.x);
        const v8sf dy = float8(d.y);
        const v8sf dz = float8(d.z);
        for(size_t i=0; i<faces.size; i+=8) {
            const v8sf Ax = *(v8sf*)(X[0].data+i);
            const v8sf Ay = *(v8sf*)(Y[0].data+i);
            const v8sf Az = *(v8sf*)(Z[0].data+i);
            const v8sf Bx = *(v8sf*)(X[1].data+i);
            const v8sf By = *(v8sf*)(Y[1].data+i);
            const v8sf Bz = *(v8sf*)(Z[1].data+i);
            const v8sf Cx = *(v8sf*)(X[2].data+i);
            const v8sf Cy = *(v8sf*)(Y[2].data+i);
            const v8sf Cz = *(v8sf*)(Z[2].data+i);
            v8sf unused det, U, V;
            const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Ox,Oy,Oz, dx,dy,dz, det,U,V);
            v8sf hmin = ::hmin(t);
            const float hmin0 = hmin[0];
            if(hmin0 >= value) continue;
            value = hmin0;
            index = i + ::indexOfEqual(t, hmin);
        }
        return index;
    }

    inline size_t raycast(vec3 O, vec3 d, float& minT, float& u, float& v) const {
        assert(faces.size < faces.capacity && align(8, faces.size)==faces.capacity);
        minT = inff; size_t index = faces.size;
        const v8sf Ox = float8(O.x);
        const v8sf Oy = float8(O.y);
        const v8sf Oz = float8(O.z);
        const v8sf dx = float8(d.x);
        const v8sf dy = float8(d.y);
        const v8sf dz = float8(d.z);
        for(size_t i=0; i<faces.size; i+=8) {
            const v8sf Ax = *(v8sf*)(X[0].data+i);
            const v8sf Ay = *(v8sf*)(Y[0].data+i);
            const v8sf Az = *(v8sf*)(Z[0].data+i);
            const v8sf Bx = *(v8sf*)(X[1].data+i);
            const v8sf By = *(v8sf*)(Y[1].data+i);
            const v8sf Bz = *(v8sf*)(Z[1].data+i);
            const v8sf Cx = *(v8sf*)(X[2].data+i);
            const v8sf Cy = *(v8sf*)(Y[2].data+i);
            const v8sf Cz = *(v8sf*)(Z[2].data+i);
            v8sf det, U, V;
            const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Ox,Oy,Oz, dx,dy,dz, det, U, V);
            v8sf hmin = ::hmin(t);
            const float hmin0 = hmin[0];
            if(hmin0 >= minT) continue;
            minT = hmin0;
            uint k = ::indexOfEqual(t, hmin);
            index = i + k;
            u = U[k]/det[k];
            v = V[k]/det[k];
        }
        return index;
    }

    inline size_t raycast_reverseWinding(vec3 O, vec3 d, float& minT, float& u, float& v) const {
        assert(faces.size < faces.capacity && align(8, faces.size)==faces.capacity);
        minT = inff; size_t index = faces.size;
        const v8sf Ox = float8(O.x);
        const v8sf Oy = float8(O.y);
        const v8sf Oz = float8(O.z);
        const v8sf dx = float8(d.x);
        const v8sf dy = float8(d.y);
        const v8sf dz = float8(d.z);
        for(size_t i=0; i<faces.size; i+=8) {
            const v8sf Ax = *(v8sf*)(X[3].data+i);
            const v8sf Ay = *(v8sf*)(Y[3].data+i);
            const v8sf Az = *(v8sf*)(Z[3].data+i);
            const v8sf Bx = *(v8sf*)(X[2].data+i);
            const v8sf By = *(v8sf*)(Y[2].data+i);
            const v8sf Bz = *(v8sf*)(Z[2].data+i);
            const v8sf Cx = *(v8sf*)(X[1].data+i);
            const v8sf Cy = *(v8sf*)(Y[1].data+i);
            const v8sf Cz = *(v8sf*)(Z[1].data+i);
            v8sf det, U, V;
            const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Ox,Oy,Oz, dx,dy,dz, det, U, V);
            v8sf hmin = ::hmin(t);
            const float hmin0 = hmin[0];
            if(hmin0 >= minT) continue;
            minT = hmin0;
            uint k = ::indexOfEqual(t, hmin);
            index = i + k;
            u = U[k]/det[k];
            v = V[k]/det[k];
        }
        return index;
    }

    inline v8ui raycast(const v8sf Ox, const v8sf Oy, const v8sf Oz, const v8sf dx, const v8sf dy, const v8sf dz) const {
        v8sf value = float8(inff); v8ui index = uintX(faces.size);
        for(size_t i: range(faces.size)) {
            const float Ax = X[0][i];
            const float Ay = Y[0][i];
            const float Az = Z[0][i];
            const float Bx = X[1][i];
            const float By = Y[1][i];
            const float Bz = Z[1][i];
            const float Cx = X[2][i];
            const float Cy = Y[2][i];
            const float Cz = Z[2][i];
            v8sf det, U, V;
            const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Ox,Oy,Oz, dx,dy,dz, det, U, V);
            index = blend(index, i, t < value);
            value = ::min(value, t);
        }
        return index;
    }

    inline v8ui raycast(const v8sf Ox, const v8sf Oy, const v8sf Oz, const v8sf dx, const v8sf dy, const v8sf dz, v8sf& minT, v8sf& u, v8sf& v) const {
        minT = inff; v8ui index = uintX(faces.size);
        for(size_t i: range(faces.size)) {
            const float Ax = X[0][i];
            const float Ay = Y[0][i];
            const float Az = Z[0][i];
            const float Bx = X[1][i];
            const float By = Y[1][i];
            const float Bz = Z[1][i];
            const float Cx = X[2][i];
            const float Cy = Y[2][i];
            const float Cz = Z[2][i];
            v8sf det, U, V;
            const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Ox,Oy,Oz, dx,dy,dz, det, U, V);
            index = blend(index, i, t < minT);
            u = blend(u, U/det, t < minT);
            v = blend(v, V/det, t < minT);
            minT = ::min(minT, t);
        }
        return index;
    }

    enum Bounce { Direct=1, Diffuse, Specular, Max };
    typedef Bounce Path[3];
    typedef uint64 Timers[Max*Max*Max];
    bgr3f shade(uint faceIndex, const vec3 P, const unused vec3 D, const vec3 T, const vec3 B, const vec3 N, Random& random, const int bounce, Path path, uint64* const timers, const size_t stride /*Max^bounce*/) /*const*/ {
        const uint64 start = readCycleCounter();
        const Scene::Face& face = faces[faceIndex];
        bgr3f out (emittanceB[faceIndex], emittanceG[faceIndex], emittanceR[faceIndex]);
        const bgr3f reflectance (reflectanceB[faceIndex],reflectanceG[faceIndex],reflectanceR[faceIndex]);
        if(specular && face.reflect) {
            if(bounce > 0) return 0; // +S
            //if(bounce > 1) return 0; // -SDS
            path[bounce] = Specular; // S
            static constexpr int iterations = 1;
            bgr3f sum = 0;
            const vec3 R = normalize(D - 2*dot(N, D)*N);
            for(uint unused i: range(iterations)) {
                const Vec<v8sf, 3> H = hemisphere(random, N);
                const v8sf RGx = R.x + face.gloss * H._[0];
                const v8sf RGy = R.y + face.gloss * H._[1];
                const v8sf RGz = R.z + face.gloss * H._[2];
                const v8sf L = sqrt(sq(RGx)+sq(RGy)+sq(RGz));
                const v8sf Rx = RGx/L;
                const v8sf Ry = RGy/L;
                const v8sf Rz = RGz/L;
                v8sf t,u,v;
                v8ui reflectRayFaceIndex = raycast(P.x,P.y,P.z, Rx,Ry,Rz, t,u,v);
                for(uint k: range(8)) {
                    if(reflectRayFaceIndex[k]==faces.size) continue;
                    vec3 R = vec3(Rx[k],Ry[k],Rz[k]);
                    sum += shade(reflectRayFaceIndex[k], P+t[k]*R, R, u[k], v[k], random, bounce+1, path, timers+Specular*stride, stride*Max);
                }
            }
            out += reflectance * (1.f/(iterations*8)) * sum;
        }
        /*else if(face.refract) {
            if(bounce > 0) return 0;
            const float n1 = 1, n2 = 1; //1.3
            const vec3 R = normalize(refract(n1/n2, N, D));
            float backT, backU, backV;
            const size_t backFaceIndex = raycast_reverseWinding(P, R, backT, backU, backV);
            if(backFaceIndex == faces.size) return 0; // FIXME

            const vec3 backP = P+backT*R;

            // Volumetric diffuse
            const float l = 1;
            float a = __builtin_exp(-backT/l);
            bgr3f volumeColor (this->emittanceB[faceIndex],this->emittanceG[faceIndex],this->emittanceR[faceIndex]);

            const vec3 backN = (1-backU-backV) * faces[backFaceIndex].N[0] +
                                         backU * faces[backFaceIndex].N[1] +
                                         backV * faces[backFaceIndex].N[2];
            const vec3 backR = refract(n2/n1, -backN, R);

            bgr3f transmitColor = raycast_shade(backP, backR, random, bounce+1);

            return (1-a)*volumeColor + a*transmitColor;
        }*/
        else
        { // Diffuse
            // Direct lighting
            bgr3f sum;
            uint directIterations;
            if(rasterize) {
                const uint lightFaceIndex = lights[0]; // FIXME: rectangle
                const uint l = bounce==0 ? L : 1;
                directIterations = l*Lookup::S;
                sum = 0;
                if(!(faceIndex == lightFaceIndex || faceIndex == lightFaceIndex+1)) {
                    //assert_(lightFaceIndex == faces.size-2);
                    const float sin = random()[0], cos = sqrt(1-sin*sin);
                    const float Tx = cos*T.x + sin*B.x;
                    const float Ty = cos*T.y + sin*B.y;
                    const float Tz = cos*T.z + sin*B.z;
                    const float Bx = cos*B.x - sin*T.x;
                    const float By = cos*B.y - sin*T.y;
                    const float Bz = cos*B.z - sin*T.z;
                    const float m00 = Tx,  m01 = Ty,  m02 = Tz;
                    const float m10 = Bx,  m11 = By,  m12 = Bz;
                    const float m20 = N.x, m21 = N.y, m22 = N.z;
                    for(const uint k: range(l)) { const Lookup& lookup = lookups[k];
                        Lookup::mask occluders = {};
                        for(uint i=0; i<faces.size-2; i+=8) { // FIXME: only (PVS) occluders (not behind any light)
                            const v8sf X0 = *(v8sf*)(X[0].data+i)-P.x;
                            const v8sf Y0 = *(v8sf*)(Y[0].data+i)-P.y;
                            const v8sf Z0 = *(v8sf*)(Z[0].data+i)-P.z;
                            const v8sf X1 = *(v8sf*)(X[1].data+i)-P.x;
                            const v8sf Y1 = *(v8sf*)(Y[1].data+i)-P.y;
                            const v8sf Z1 = *(v8sf*)(Z[1].data+i)-P.z;
                            const v8sf X2 = *(v8sf*)(X[2].data+i)-P.x;
                            const v8sf Y2 = *(v8sf*)(Y[2].data+i)-P.y;
                            const v8sf Z2 = *(v8sf*)(Z[2].data+i)-P.z;
                            const v8sf D0 = N.x * X0 + N.y * Y0 + N.z * Z0;
                            const v8sf D1 = N.x * X1 + N.y * Y1 + N.z * Z1;
                            const v8sf D2 = N.x * X2 + N.y * Y2 + N.z * Z2;
                            const v8si cull = D0 <= 0 || D1 <= 0 || D2 <= 0;
                            // FIXME: cull/repack here ?
                            v8sf C0x, C0y, C0z; cross(X2,Y2,Z2, X1,Y1,Z1, C0x,C0y,C0z);
                            v8sf C1x, C1y, C1z; cross(X0,Y0,Z0, X2,Y2,Z2, C1x,C1y,C1z);
                            v8sf C2x, C2y, C2z; cross(X1,Y1,Z1, X0,Y0,Z0, C2x,C2y,C2z);
                            const v8sf N0x = m00 * C0x + m01 * C0y + m02 * C0z;
                            const v8sf N0y = m10 * C0x + m11 * C0y + m12 * C0z;
                            const v8sf N0z = m20 * C0x + m21 * C0y + m22 * C0z;
                            const v8sf N1x = m00 * C1x + m01 * C1y + m02 * C1z;
                            const v8sf N1y = m10 * C1x + m11 * C1y + m12 * C1z;
                            const v8sf N1z = m20 * C1x + m21 * C1y + m22 * C1z;
                            const v8sf N2x = m00 * C2x + m01 * C2y + m02 * C2z;
                            const v8sf N2y = m10 * C2x + m11 * C2y + m12 * C2z;
                            const v8sf N2z = m20 * C2x + m21 * C2y + m22 * C2z;
                            const v8si lookup0 = lookup.index(N0x, N0y, N0z);
                            const v8si lookup1 = lookup.index(N1x, N1y, N1z);
                            const v8si lookup2 = lookup.index(N2x, N2y, N2z);
                            for(uint k: range(8)) { // Cannot gather 256bit loads
                                if(cull[k] || i+k >= faces.size-2) continue; // FIXME
                                const Lookup::mask occluder = lookup.lookup[lookup0[k]] & lookup.lookup[lookup1[k]] & lookup.lookup[lookup2[k]];
                                occluders |= occluder;
                            }
                        }

                        const vec3 v00 = vec3(X[0][lightFaceIndex], Y[0][lightFaceIndex], Z[0][lightFaceIndex])-P;
                        const vec3 v01 = vec3(X[1][lightFaceIndex], Y[1][lightFaceIndex], Z[1][lightFaceIndex])-P;
                        const vec3 v11 = vec3(X[2][lightFaceIndex], Y[2][lightFaceIndex], Z[2][lightFaceIndex])-P;
                        const vec3 v10 = vec3(X[2][lightFaceIndex+1], Y[2][lightFaceIndex+1], Z[2][lightFaceIndex+1])-P;
                        const vec3 n0g = cross(v00, v10);
                        const vec3 n1g = cross(v10, v11);
                        const vec3 n2g = cross(v11, v01);
                        const vec3 n3g = cross(v01, v00);
                        const vec3 n0 = vec3(m00*n0g.x + m01*n0g.y + m02*n0g.z, m10*n0g.x + m11*n0g.y + m12*n0g.z, m20*n0g.x + m21*n0g.y + m22*n0g.z);
                        const vec3 n1 = vec3(m00*n1g.x + m01*n1g.y + m02*n1g.z, m10*n1g.x + m11*n1g.y + m12*n1g.z, m20*n1g.x + m21*n1g.y + m22*n1g.z);
                        const vec3 n2 = vec3(m00*n2g.x + m01*n2g.y + m02*n2g.z, m10*n2g.x + m11*n2g.y + m12*n2g.z, m20*n2g.x + m21*n2g.y + m22*n2g.z);
                        const vec3 n3 = vec3(m00*n3g.x + m01*n3g.y + m02*n3g.z, m10*n3g.x + m11*n3g.y + m12*n3g.z, m20*n3g.x + m21*n3g.y + m22*n3g.z);
                        Lookup::mask light = lookup(n0) & lookup(n1) & lookup(n2) & lookup(n3); // FIXME: SIMD all lights

                        const uint lightSum = popcount(light & ~occluders);
                        const float factor = (float) lightSum;
                        sum.b += emittanceB[lightFaceIndex] * factor;
                        sum.g += emittanceG[lightFaceIndex] * factor;
                        sum.r += emittanceR[lightFaceIndex] * factor;
                    }
                    count += l;
                }
            } else  {
                v8sf sumB=0, sumG=0, sumR=0;
                directIterations = (bounce==0?L:1)*8;
                for(uint unused i: range(directIterations/8)) {
                    const Vec<v8sf, 3> l = cosine(random);
                    const v8sf Lx = T.x * l._[0] + B.x * l._[1] + N.x * l._[2];
                    const v8sf Ly = T.y * l._[0] + B.y * l._[1] + N.y * l._[2];
                    const v8sf Lz = T.z * l._[0] + B.z * l._[1] + N.z * l._[2];
                    const float factor  = 1; // Cosine factor already applied by cosine sampling distribution
                    const v8ui lightRayFaceIndex = raycast(P.x,P.y,P.z, Lx,Ly,Lz);
                    sumB += factor * gather(emittanceB.data, lightRayFaceIndex);
                    sumG += factor * gather(emittanceG.data, lightRayFaceIndex);
                    sumR += factor * gather(emittanceR.data, lightRayFaceIndex);
                }
                sum = bgr3f(hsum(sumB), hsum(sumG), hsum(sumR));
            }
#if RT
            static constexpr int indirectIterations = 1;
#else
            const int indirectIterations = bounce < 1 ? 512 : 0;
#endif
            const float scale = 1.f/(directIterations+indirectIterations*8);
            if(indirect && bounce < 1) { // Indirect diffuse lighting (TODO: radiosity)
                path[bounce] = Diffuse;
                for(uint unused i: range(indirectIterations)) {
                    const Vec<v8sf, 3> l = cosine(random);
                    const v8sf Lx = T.x * l._[0] + B.x * l._[1] + N.x * l._[2];
                    const v8sf Ly = T.y * l._[0] + B.y * l._[1] + N.y * l._[2];
                    const v8sf Lz = T.z * l._[0] + B.z * l._[1] + N.z * l._[2];
                    v8sf t,u,v;
                    v8ui lightRayFaceIndex = raycast(P.x,P.y,P.z, Lx,Ly,Lz, t,u,v);
                    for(uint k: range(8)) {
                        vec3 L = vec3(Lx[k],Ly[k],Lz[k]);
                        if(lightRayFaceIndex[k]!=faces.size) sum += shade(lightRayFaceIndex[k], P+t[k]*L, L, u[k], v[k], random, bounce+1, path, timers+Diffuse*stride, stride*Max);
                    }
                }
            } else
                path[bounce] = Direct;
            out += scale * reflectance * sum;
        }
        timers[path[bounce]*stride] += readCycleCounter()-start;
        return out;
    }

    inline bgr3f shade(size_t faceIndex, const vec3 P, const vec3 D, const float u, const float v, Random& random, const uint bounce, Path path, uint64* const timers, const size_t stride /*Max^bounce*/) /*const*/ {
        const vec3 T = (1-u-v) * faces[faceIndex].T[0] +
                u * faces[faceIndex].T[1] +
                v * faces[faceIndex].T[2];
        const vec3 B = (1-u-v) * faces[faceIndex].B[0] +
                u * faces[faceIndex].B[1] +
                v * faces[faceIndex].B[2];
        const vec3 N = (1-u-v) * faces[faceIndex].N[0] +
                u * faces[faceIndex].N[1] +
                v * faces[faceIndex].N[2];
        return shade(faceIndex, P, D, T, B, N, random, bounce, path, timers, stride);
    }

    inline bgr3f raycast_shade(const vec3 O, const vec3 D, Random& random, const uint bounce, Path path, uint64* const timers, const size_t stride) /*const*/;

    struct NoShader {
        static constexpr int C = 0;
        static constexpr int V = 0;
        typedef uint FaceAttributes;
        inline Vec<float, 0> shade(const uint, FaceAttributes, float, float[V]) const { return {}; }
        inline Vec<v16sf, C> shade(const uint, FaceAttributes, v16sf, v16sf[V], v16si) const { return {}; }
    };

    struct CheckerboardShader {
        static constexpr int C = 3;
        static constexpr int V = 2;
        typedef uint FaceAttributes;

        inline Vec<float, 3> shade(const uint, FaceAttributes, float, float varying[2]) const { return shade(varying); }
        inline Vec<v16sf, 3> shade(const uint, FaceAttributes, v16sf, v16sf varying[2], v16si) const { return shade(varying); }

        template<Type T> inline Vec<T, 3> shade(T varying[2]) const {
            const T u = varying[0], v = varying[1];
            static T cellCount = T(1); //static T cellCount = T(16);
            const T n = floor(cellCount*u)+floor(cellCount*v); // Integer
            const T m = T(1./2)*n; // Half integer
            const T mod = T(2)*(m-floor(m)); // 0 or 1, 2*fract(n/2) = n%2
            return Vec<T, 3>{{mod, mod, mod}};
        }
    };

    template<int C_, int V_, Type D> struct Shader {
        static constexpr int C = C_;
        static constexpr int V = V_;
        typedef uint FaceAttributes;

        inline Vec<v16sf, C> shade(const uint id, FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const {
            Vec<v16sf, C> Y;
            for(uint i: range(16)) {
                if(!mask[i]) continue;
                float x[V];
                for(uint v: range(V)) x[v] = varying[v][i];
                Vec<float, C> y = ((D*)this)->shade(id, face, z[i], x);
                for(uint c: range(C)) Y._[c][i] = y._[c];
            }
            return Y;
        }
    };

    struct TextureShader : Shader<3, 2, TextureShader> {
        struct Face {
            const Float* BGR[3];
            v8si sample4D;
            v4sf Wts;
        };

        buffer<Face> faces;

        TextureShader(const Scene& scene) : faces(scene.faces.size) {}

        void setFaceAttributes(const ref<Scene::Face>& faces, const uint sSize, const uint tSize, const float S, const float T) {
            assert_(faces.size == this->faces.size);
            const float s = ::min(S * (sSize-1), sSize-1-0x1p-18f);
            const float t = ::min(T * (tSize-1), tSize-1-0x1p-18f);
            const size_t sIndex = s;
            const size_t tIndex = t;
            for(size_t faceIndex: range(faces.size)) {
                const Scene::Face& face = faces[faceIndex];
                Face& attributes = this->faces[faceIndex];
                const int    size1 = face.size.x*1    ;
                const int    size2 = face.size.y*size1;
                const int    size3 = sSize      *size2;
                const size_t size4 = tSize      *size3;
                attributes.BGR[0] = face.BGR + 0*size4 + tIndex*size3 + sIndex*size2;
                attributes.BGR[1] = face.BGR + 1*size4 + tIndex*size3 + sIndex*size2;
                attributes.BGR[2] = face.BGR + 2*size4 + tIndex*size3 + sIndex*size2;
#if HALF // Half
                attributes.sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                           size3/2,   (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
                if(sSize == 1 || tSize ==1) // Prevents OOB
                    attributes.sample4D = {    0,           size1/2,         0,       size1/2,
                                               0,           size1/2,         0,       size1/2};
#else  // Single
                attributes.sample4D = {    0,           size1,         size2,       (size2+size1),
                                           size3,   (size3+size1), (size3+size2), (size3+size2+size1)};
                if(sSize == 1 || tSize ==1) // Prevents OOB
                    attributes.sample4D = {    0,           size1,         0,       size1,
                                               0,           size1,         0,       size1};
#endif
                attributes.Wts = {(1-fract(t))*(1-fract(s)), (1-fract(t))*fract(s), fract(t)*(1-fract(s)), fract(t)*fract(s)};
            }
        }

        inline Vec<v16sf, C> shade(const uint id, FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const { return Shader::shade(id, face, z, varying, mask); }
        inline Vec<float, 3> shade(const uint, FaceAttributes index, float, float varying[V]) const {
            const float u = varying[0], v = varying[1];
            const int vIndex = v, uIndex = u; // Floor
            const Face& face = faces[index];
#if HALF // Half
            const size_t base = 2*face.sample4D[1]*vIndex + uIndex;
            const v16sf B = toFloat((v16hf)gather((float*)(face.BGR[0]+base), face.sample4D));
            const v16sf G = toFloat((v16hf)gather((float*)(face.BGR[1]+base), face.sample4D));
            const v16sf R = toFloat((v16hf)gather((float*)(face.BGR[2]+base), face.sample4D));
#else  // Single
            const size_t base = face.sample4D[1]*vIndex + uIndex;
            const v8sf b0 = gather((float*)(face.BGR[0]+base), face.sample4D);
            const v8sf b1 = gather((float*)(face.BGR[0]+base+1), face.sample4D);
            const v16sf B = shuffle(b0, b1, 0, 8+0, 1, 8+1, 2, 8+2, 3, 8+3, 4, 8+4, 5, 8+5, 6, 8+6, 7, 8+7);
            const v8sf g0 = gather((float*)(face.BGR[1]+base), face.sample4D);
            const v8sf g1 = gather((float*)(face.BGR[1]+base+1), face.sample4D);
            const v16sf G = shuffle(g0, g1, 0, 8+0, 1, 8+1, 2, 8+2, 3, 8+3, 4, 8+4, 5, 8+5, 6, 8+6, 7, 8+7);
            const v8sf r0 = gather((float*)(face.BGR[2]+base), face.sample4D);
            const v8sf r1 = gather((float*)(face.BGR[2]+base+1), face.sample4D);
            const v16sf R = shuffle(r0, r1, 0, 8+0, 1, 8+1, 2, 8+2, 3, 8+3, 4, 8+4, 5, 8+5, 6, 8+6, 7, 8+7);
#endif
            const v4sf Wts = face.Wts;
            const v4sf vuvu = {v, u, v, u};
            const v4sf w_1mw = abs(vuvu - floor(vuvu) - _1100f); // 1-fract(x), fract(x)
            const v16sf w01 = shuffle(  Wts,   Wts, 0,0,0,0,1,1,1,1, 2,2,2,2,3,3,3,3)  // 0000111122223333
                    * shuffle(w_1mw, w_1mw, 0,0,2,2,0,0,2,2, 0,0,2,2,0,0,2,2)  // vvVVvvVVvvVVvvVV
                    * shuffle(w_1mw, w_1mw, 1,3,1,3,1,3,1,3, 1,3,1,3,1,3,1,3); // uUuUuUuUuUuUuUuU
            return Vec<float, 3>{{dot(w01, B), dot(w01, G), dot(w01, R)}};
        }
    };

    struct RaycastShader : Shader<3, 8, RaycastShader> {
        typedef uint FaceAttributes;

        /*const*/ Scene& scene;
        vec3 viewpoint; // (s, t)
        buffer<Random> randoms;

        RaycastShader(/*const*/ Scene& scene) : scene(scene) {
            randoms = buffer<Random>(threadCount());
            for(auto& random: randoms) random=Random();
        }

        inline Vec<v16sf, C> shade(const uint id, FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const { return Shader::shade(id, face, z, varying, mask); }
        inline Vec<float, 3> shade(const uint id, FaceAttributes index, float, float unused varying[V]) const {
            const vec3 P = vec3(varying[2], varying[3], varying[4]);
            const vec3 T = normalize(vec3(varying[5], varying[6], varying[7]));
            const vec3 B = normalize(vec3(varying[8], varying[9], varying[10]));
            const vec3 N = normalize(vec3(varying[11], varying[12], varying[13])); // FIXME: cross
            Scene::Timers timers;
            bgr3f color = scene.shade(index, P, normalize(P-viewpoint), T, B, N, randoms[id], 0, {}, timers, Max);
            return Vec<float, 3>{{color.b, color.g, color.r}};
        }
    };

    template<Type Shader> struct Renderer {
        Shader shader; // Instance holds any uniforms (currently none)
        RenderPass<Shader> pass; // Face bins
        RenderTarget<Shader::C> target; // Sample tiles
        Renderer(Shader&& shader_={}) : shader(::move(shader_)), pass(shader) {}
    };

    template<Type Shader, Type... Args>
    void render(Renderer<Shader>& renderer, mat4 M, float clear[/*sizeof...(Args)*/], const ImageH& depth, const Args&... targets_) {
        const ImageH targets[sizeof...(Args)] { unsafeShare(targets_)... }; // Zero-length arrays are not permitted in C++
        uint2 size = (sizeof...(Args) ? targets[0] : depth).size;
        renderer.target.setup(int2(size), 1, clear); // Needs to be setup before pass
        renderer.pass.setup(renderer.target, ref<Face>(faces).size * 2 /* 1 quad face = 2 triangles */ ); // Clears bins face counter
        mat4 NDC;
        NDC.scale(vec3(vec2(size*4u)/2.f, 1)); // 0, 2 -> subsample size // *4u // MSAA->4x
        NDC.translate(vec3(vec2(1), 0.f)); // -1, 1 -> 0, 2
        M = NDC * M;
        for(size_t faceIndex : range(faces.size)) {
            const vec3 A (X[0][faceIndex], Y[0][faceIndex], Z[0][faceIndex]);
            const vec3 B (X[1][faceIndex], Y[1][faceIndex], Z[1][faceIndex]);
            const vec3 C (X[2][faceIndex], Y[2][faceIndex], Z[2][faceIndex]);

            const vec4 a = M*vec4(A,1), b = M*vec4(B,1), c = M*vec4(C,1);
            if(cross((b/b.w-a/a.w).xyz(),(c/c.w-a/a.w).xyz()).z >= 0) continue; // Backward face culling

            const Face& face = faces[faceIndex];
            renderer.pass.submit(a,b,c, (vec3[]){vec3(face.u[0],face.u[1],face.u[2]),
                                                 vec3(face.v[0],face.v[1],face.v[2]),
                                                 vec3(A.x,B.x,C.x),
                                                 vec3(A.y,B.y,C.y),
                                                 vec3(A.z,B.z,C.z),
                                                 vec3(faces[faceIndex].T[0].x, faces[faceIndex].T[1].x, faces[faceIndex].T[2].x),
                                                 vec3(faces[faceIndex].T[0].y, faces[faceIndex].T[1].y, faces[faceIndex].T[2].y),
                                                 vec3(faces[faceIndex].T[0].z, faces[faceIndex].T[1].z, faces[faceIndex].T[2].z),
                                                 vec3(faces[faceIndex].B[0].x, faces[faceIndex].B[1].x, faces[faceIndex].B[2].x),
                                                 vec3(faces[faceIndex].B[0].y, faces[faceIndex].B[1].y, faces[faceIndex].B[2].y),
                                                 vec3(faces[faceIndex].B[0].z, faces[faceIndex].B[1].z, faces[faceIndex].B[2].z),
                                                 vec3(faces[faceIndex].N[0].x, faces[faceIndex].N[1].x, faces[faceIndex].N[2].x),
                                                 vec3(faces[faceIndex].N[0].y, faces[faceIndex].N[1].y, faces[faceIndex].N[2].y),
                                                 vec3(faces[faceIndex].N[0].z, faces[faceIndex].N[1].z, faces[faceIndex].N[2].z) // FIXME: cross
                                 }, faceIndex);
        }
        renderer.pass.render(renderer.target);
        renderer.target.resolve(depth, targets);
    }
};

inline bgr3f Scene::raycast_shade(const vec3 O, const vec3 D, Random& random, const uint bounce, Path path, uint64* const timers, const size_t stride) /*const*/ {
    float t, u, v;
    const size_t faceIndex = raycast(O, D, t, u, v);
    return t<inff ? shade(faceIndex, O+t*D, D, u, v, random, bounce, path, timers, stride) : 0;
}

Scene parseScene(ref<byte> scene);

inline string basename(string x) {
    string name = x.contains('/') ? section(x,'/',-2,-1) : x;
    string basename = name.contains('.') ? section(name,'.',0,-2) : name;
    assert_(basename);
    return basename;
}

#include "file.h"

inline String sceneFile(string name) {
    if(existsFile(name)) return name+"/scene.json";
    if(existsFile(name+".scene")) return name+".scene";
    if(existsFile(name+".obj")) return name+".obj";
    error("No such file", name);
}
