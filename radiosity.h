#pragma once
#include "scene.h"
#include "mwc.h"
#include "sphere.h"

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
    //typedef uint8 mask;
    typedef v4uq mask;
    static constexpr uint S = sizeof(mask)*8;
    static constexpr uint N = 128;
    buffer<float> X {S}, Y{S}, Z{S};
    buffer<mask> lookup {N*N}; // 512K

    inline v8ui index(const v8sf X, const v8sf Y, const v8sf Z) const {
        const Vec<v8sf, 2> UV = square(X, Y, Z);
        const v8ui uIndex = (v8ui)cvtt((UV._[0]+1)*((N-1)/2.f));
        const v8ui vIndex = (v8ui)cvtt((UV._[1]+1)*((N-1)/2.f));
        //for(uint k: range(8)) assert_(uIndex[k] >= 0 && uIndex[k] < N && vIndex[k] >= 0 && vIndex[k] < N, UV._[0][k], UV._[1][k], X[k], Y[k], Z[k]);
        return vIndex*N+uIndex;
    }

    void generate(Random& random) {
        // FIXME: stratified
        for(uint i=0; i<S; i+=8) {
            Vec<v8sf, 3> s8 = cosine(random);
            *(v8sf*)(X.begin()+i) = s8._[0];
            *(v8sf*)(Y.begin()+i) = s8._[1];
            *(v8sf*)(Z.begin()+i) = s8._[2];
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
                    uint8* mask = (uint8*)(span8+k);
                    for(const uint i: range(S/8)) mask[i] = ::mask((x*(*(v8sf*)(X.begin()+i*8)) + y*(*(v8sf*)(Y.begin()+i*8)) + z*(*(v8sf*)(Z.begin()+i*8))) >= 0);
                }
            }
        }
    }

    inline mask operator()(const vec3 n) const {
        const uint index = this->index(n.x, n.y, n.z)[0];
        return lookup[index];
    }
};

struct Radiosity {
    const Scene& scene;
    Lookup lookup;

    Radiosity(const Scene& scene) : scene(scene) {}

    inline size_t raycast(vec3 O, vec3 d) const {
        assert(scene.size < scene.capacity && align(8, scene.size)==scene.capacity);
        float value = inff; size_t index = scene.size;
        const v8sf Ox = float8(O.x);
        const v8sf Oy = float8(O.y);
        const v8sf Oz = float8(O.z);
        const v8sf dx = float8(d.x);
        const v8sf dy = float8(d.y);
        const v8sf dz = float8(d.z);
        for(size_t i=0; i<scene.size; i+=8) {
            const v8sf Ax = *(v8sf*)(scene.X0.data+i);
            const v8sf Ay = *(v8sf*)(scene.Y0.data+i);
            const v8sf Az = *(v8sf*)(scene.Z0.data+i);
            const v8sf Bx = *(v8sf*)(scene.X1.data+i);
            const v8sf By = *(v8sf*)(scene.Y1.data+i);
            const v8sf Bz = *(v8sf*)(scene.Z1.data+i);
            const v8sf Cx = *(v8sf*)(scene.X2.data+i);
            const v8sf Cy = *(v8sf*)(scene.Y2.data+i);
            const v8sf Cz = *(v8sf*)(scene.Z2.data+i);
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
        assert(scene.size < scene.capacity && align(8, scene.size)==scene.capacity);
        minT = inff; size_t index = scene.size;
        const v8sf Ox = float8(O.x);
        const v8sf Oy = float8(O.y);
        const v8sf Oz = float8(O.z);
        const v8sf dx = float8(d.x);
        const v8sf dy = float8(d.y);
        const v8sf dz = float8(d.z);
        for(size_t i=0; i<scene.size; i+=8) {
            const v8sf Ax = *(v8sf*)(scene.X0.data+i);
            const v8sf Ay = *(v8sf*)(scene.Y0.data+i);
            const v8sf Az = *(v8sf*)(scene.Z0.data+i);
            const v8sf Bx = *(v8sf*)(scene.X1.data+i);
            const v8sf By = *(v8sf*)(scene.Y1.data+i);
            const v8sf Bz = *(v8sf*)(scene.Z1.data+i);
            const v8sf Cx = *(v8sf*)(scene.X2.data+i);
            const v8sf Cy = *(v8sf*)(scene.Y2.data+i);
            const v8sf Cz = *(v8sf*)(scene.Z2.data+i);
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

#if 0
    inline size_t raycast_reverseWinding(vec3 O, vec3 d, float& minT, float& u, float& v) const {
        assert(scene.size < scene.capacity && align(8, scene.size)==scene.capacity);
        minT = inff; size_t index = scene.size;
        const v8sf Ox = float8(O.x);
        const v8sf Oy = float8(O.y);
        const v8sf Oz = float8(O.z);
        const v8sf dx = float8(d.x);
        const v8sf dy = float8(d.y);
        const v8sf dz = float8(d.z);
        for(size_t i=0; i<scene.size; i+=8) {
            const v8sf Ax = *(v8sf*)(scene.X2.data+i);
            const v8sf Ay = *(v8sf*)(scene.Y2.data+i);
            const v8sf Az = *(v8sf*)(scene.Z2.data+i);
            const v8sf Bx = *(v8sf*)(scene.X1.data+i);
            const v8sf By = *(v8sf*)(scene.Y1.data+i);
            const v8sf Bz = *(v8sf*)(scene.Z1.data+i);
            const v8sf Cx = *(v8sf*)(scene.X0.data+i);
            const v8sf Cy = *(v8sf*)(scene.Y0.data+i);
            const v8sf Cz = *(v8sf*)(scene.Z0.data+i);
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
#endif

    inline v8ui raycast(const v8sf Ox, const v8sf Oy, const v8sf Oz, const v8sf dx, const v8sf dy, const v8sf dz) const {
        v8sf value = float8(inff); v8ui index = uintX(scene.size);
        for(size_t i: range(scene.size)) {
            const float Ax = scene.X0[i];
            const float Ay = scene.Y0[i];
            const float Az = scene.Z0[i];
            const float Bx = scene.X1[i];
            const float By = scene.Y1[i];
            const float Bz = scene.Z1[i];
            const float Cx = scene.X2[i];
            const float Cy = scene.Y2[i];
            const float Cz = scene.Z2[i];
            v8sf det, U, V;
            const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Ox,Oy,Oz, dx,dy,dz, det, U, V);
            index = blend(index, i, t < value);
            value = ::min(value, t);
        }
        return index;
    }

    inline v8ui raycast(const v8sf Ox, const v8sf Oy, const v8sf Oz, const v8sf dx, const v8sf dy, const v8sf dz, v8sf& minT, v8sf& u, v8sf& v) const {
        minT = inff; v8ui index = uintX(scene.size);
        for(size_t i: range(scene.size)) {
            const float Ax = scene.X0[i];
            const float Ay = scene.Y0[i];
            const float Az = scene.Z0[i];
            const float Bx = scene.X1[i];
            const float By = scene.Y1[i];
            const float Bz = scene.Z1[i];
            const float Cx = scene.X2[i];
            const float Cy = scene.Y2[i];
            const float Cz = scene.Z2[i];
            v8sf det, U, V;
            const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Ox,Oy,Oz, dx,dy,dz, det, U, V);
            index = blend(index, i, t < minT);
            u = blend(u, U/det, t < minT);
            v = blend(v, V/det, t < minT);
            minT = ::min(minT, t);
        }
        return index;
    }

    bgr3f shade(uint faceIndex, const vec3 P, const unused vec3 D, const vec3 T, const vec3 B, const vec3 N, Random& random) const {
        bgr3f out (scene.emittanceB[faceIndex], scene.emittanceG[faceIndex], scene.emittanceR[faceIndex]);
        const bgr3f reflectance (scene.reflectanceB[faceIndex], scene.reflectanceG[faceIndex], scene.reflectanceR[faceIndex]);
#if 0
        if(specular && face.reflect) { // FIXME: => face.reflect
            if(!texCount) return 0; // FIXME: => specular
            if(bounce > 0) return 0; // SS
            //if(bounce > 1) return 0; // -SDS
            //path[bounce] = Specular; // S
            // TODO: Hemispherical rasterization glossy reflection
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
                    if(reflectRayFaceIndex[k]==scene.size) continue;
                    vec3 R = vec3(Rx[k],Ry[k],Rz[k]);
                    if(0) sum += shade(reflectRayFaceIndex[k], P+t[k]*R, R, u[k], v[k], random, bounce+1, path, timers+Specular*stride, stride*Max);
                    else {
                        sum += shade(faces[reflectRayFaceIndex[k]], u[k], v[k]);
                        /*if(texCount) {
                            sum += shade(faces[reflectRayFaceIndex[k]], u[k], v[k]);
                        } else {

                        }*/
                    }
                }
            }
            out += reflectance * (1.f/(iterations*8*texCount)) * sum;
        } else
#endif
#if 0
        if(face.refract) {
            if(bounce > 0) return 0;
            const float n1 = 1, n2 = 1; //1.3
            const vec3 R = normalize(refract(n1/n2, N, D));
            float backT, backU, backV;
            const size_t backFaceIndex = raycast_reverseWinding(P, R, backT, backU, backV);
            if(backFaceIndex == scene.size) return 0; // FIXME

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
        } else
#endif
#if 1
        { // Diffuse
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
            float T[Lookup::S]; mref<float>(T, Lookup::S).clear(inff);
            uint id[Lookup::S]; mref<uint>(id, Lookup::S).clear(scene.size);
            const float* NX0 = scene.NX0.data, *NY0 = scene.NX1.data, *NZ0 = scene.NX2.data; // FIXME: face normal
            const float* PX0 = scene.X0.data, *PX1 = scene.X1.data, *PX2 = scene.X2.data;
            const float* PY0 = scene.Y0.data, *PY1 = scene.Y1.data, *PY2 = scene.X2.data;
            const float* PZ0 = scene.Z0.data, *PZ1 = scene.Z1.data, *PZ2 = scene.X2.data;
            const float* Sx = lookup.X.data;
            const float* Sy = lookup.Y.data;
            const float* Sz = lookup.Z.data;
            for(uint i=0; i<scene.size; i+=8) {
                const v8sf FNX = *(v8sf*)(NX0+i);
                const v8sf FNY = *(v8sf*)(NY0+i);
                const v8sf FNZ = *(v8sf*)(NZ0+i);
                //v8sf FNx, FNy, FNz; cross(X1-X0,Y1-Y0,Z1-Z0, X2-X0,Y2-Y0,Z2-Z0, FNx,FNy,FNz);
                const v8sf X0 = *(v8sf*)(PX0+i)-P.x;
                const v8sf Y0 = *(v8sf*)(PY0+i)-P.y;
                const v8sf Z0 = *(v8sf*)(PZ0+i)-P.z;
                const v8sf X1 = *(v8sf*)(PX1+i)-P.x;
                const v8sf Y1 = *(v8sf*)(PY1+i)-P.y;
                const v8sf Z1 = *(v8sf*)(PZ1+i)-P.z;
                const v8sf X2 = *(v8sf*)(PX2+i)-P.x;
                const v8sf Y2 = *(v8sf*)(PY2+i)-P.y;
                const v8sf Z2 = *(v8sf*)(PZ2+i)-P.z;
                const v8sf D0 = N.x * X0 + N.y * Y0 + N.z * Z0;
                const v8sf D1 = N.x * X1 + N.y * Y1 + N.z * Z1;
                const v8sf D2 = N.x * X2 + N.y * Y2 + N.z * Z2;
                static constexpr v8si seq {0,1,2,3,4,5,6,7};
                const v8sf D = FNX*X0 + FNY*Y0 + FNZ*Z0;
                const v8si cull = (i+seq >= scene.size) || (D0 <= 0 && D1 <= 0 && D2 <= 0) /*Plane*/ || D <= 0 /*Backward*/;
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
                // FIXME: cull/repack here ?
                const v8ui lookup0 = lookup.index(N0x, N0y, N0z);
                const v8ui lookup1 = lookup.index(N1x, N1y, N1z);
                const v8ui lookup2 = lookup.index(N2x, N2y, N2z);
                for(uint k: range(8)) {
                    if(cull[k]) continue; // FIXME
                    if(i+k == faceIndex) continue; //assert_(i+k != faceIndex, D[k]); // FIXME:
                    /*assert_(lookup0[k] < Lookup::N*Lookup::N && lookup1[k] < Lookup::N*Lookup::N && lookup2[k] < Lookup::N*Lookup::N, i, k, lookup0[k], lookup1[k], lookup2[k], N2x[k], N2y[k], N2z[k],
                            m00, m01, m02, m10, m11, m12, m20, m21, m22, C2x[k], C2y[k], C2z[k], X0[k], Y0[k], Z0[k], X1[k], Y1[k], Z1[k], D0[k], D1[k], D2[k], D[k]);*/
                    const Lookup::mask mask = lookup.lookup[lookup0[k]] & lookup.lookup[lookup1[k]] & lookup.lookup[lookup2[k]];
                    // FIXME: cull/repack here ?
                    for(uint s=0; s<Lookup::S; s+=8) {
                        const v8sf ti = D / (FNX*(*(v8sf*)(Sx+s)) + FNY*(*(v8sf*)(Sy+s)) + FNZ*(*(v8sf*)(Sz+s)));
                        v8sf& t = *(v8sf*)(T+s);
                        const v8si mask8 = ::mask(((uint8*)&mask)[s/8]) & (ti > 0) & (ti < t);
                        store(t, mask8, ti);
                        store(*(v8si*)(id+s), mask8, i+k);
                    }
                }
            }
            v8sf sumB = 0, sumG = 0, sumR = 0;
            //sumB = sumG = sumR = Lookup::S/8.f; // Ambient
            for(uint s=0; s<Lookup::S; s+=8) {
#if 1 // "AO"
                v8sf t = *(v8sf*)(T+s);
                //for(int k: range(8)) assert_(t[k]>0, t[k]);
                t = min(t, 1);
                const v8sf b = t;
                const v8sf g = t;
                const v8sf r = t;
#else
                const v8si i = *(v8si*)(id+s);
#if 1 // Direct
                const v8sf b = gather(scene.emittanceB.data, i);
                const v8sf g = gather(scene.emittanceG.data, i);
                const v8sf r = gather(scene.emittanceR.data, i);
#else // Indirect
                const v8sf Dx = *(v8sf*)(Sx+s);
                const v8sf Dy = *(v8sf*)(Sy+s);
                const v8sf Dz = *(v8sf*)(Sz+s);
                const v8sf X0 = gather(PX0, i);
                const v8sf Y0 = gather(PY0, i);
                const v8sf Z0 = gather(PZ0, i);
                const v8sf X1 = gather(PX1, i);
                const v8sf Y1 = gather(PY1, i);
                const v8sf Z1 = gather(PZ1, i);
                const v8sf X2 = gather(PX2, i);
                const v8sf Y2 = gather(PY2, i);
                const v8sf Z2 = gather(PZ2, i);
                const v8sf e02x = X2 - X0;
                const v8sf e02y = Y2 - Y0;
                const v8sf e02z = Z2 - Z0;
                v8sf Px, Py, Pz; cross(Dx, Dy, Dz, e02x, e02y, e02z, Px, Py, Pz);
                const v8sf e01x = X1 - X0;
                const v8sf e01y = Y1 - Y0;
                const v8sf e01z = Z1 - Z0;
                const v8sf det = dot(e01x, e01y, e01z, Px, Py, Pz);
                const v8sf Tx = P.x - X0;
                const v8sf Ty = P.y - Y0;
                const v8sf Tz = P.z - Z0;
                const v8si hit = i != scene.size;
                const v8ui size1 = gather(scene.size1.data, i);
                const v8sf u = and(hit, min(max(0.f, dot(Tx, Ty, Tz, Px, Py, Pz) / det), toFloat(size1-1)));
                v8sf Qx, Qy, Qz; cross(Tx, Ty, Tz, e01x, e01y, e01z, Qx, Qy, Qz);
                const v8ui V = gather(scene.V.data, i);
                const v8sf v = and(hit, min(max(0.f, dot(Dx, Dy, Dz, Qx, Qy, Qz) / det), toFloat(V-1)));
                const v8si vIndex = cvtt(v), uIndex = cvtt(u); // Floor
                const Float* const base = scene.samples.data;
                const v8ui faces = gather(scene.BGR.data, i);
                const v8ui size4 = gather(scene.size4.data, i);
                const v8ui i00 = faces + vIndex*size1 + uIndex;
                const v8ui ib00 = i00 + 0*size4;
                const v8sf b00 = gather(base, ib00);
                const v8sf b01 = gather(base, ib00 + 1);
                const v8sf b10 = gather(base, ib00 + size1);
                const v8sf b11 = gather(base, ib00 + size1 + 1);
                const v8ui ig00 = i00 + 1*size4;
                const v8sf g00 = gather(base, ig00);
                const v8sf g01 = gather(base, ig00 + 1);
                const v8sf g10 = gather(base, ig00 + size1);
                const v8sf g11 = gather(base, ig00 + size1 + 1);
                const v8ui ir00 = i00 + 2*size4;
                const v8sf r00 = gather(base, ir00);
                const v8sf r01 = gather(base, ir00 + 1);
                const v8sf r10 = gather(base, ir00 + size1);
                const v8sf r11 = gather(base, ir00 + size1 + 1);
                const v8sf fu = u-floor(u);
                const v8sf fv = v-floor(v);
                const v8sf w00 = (1-fu)*(1-fv);
                const v8sf w01 =    fu *(1-fv);
                const v8sf w10 = (1-fu)*   fv ;
                const v8sf w11 =    fu *   fv ;
                const v8sf b = w00 * b00 + w01 * b01 + w10 * b10 + w11 * b11;
                const v8sf g = w00 * g00 + w01 * g01 + w10 * g10 + w11 * g11;
                const v8sf r = w00 * r00 + w01 * r01 + w10 * r10 + w11 * r11;
#endif
#endif
                sumB += b;
                sumG += g;
                sumR += r;
            }
            out += reflectance * (1.f/(Lookup::S*scene.iterations)) * bgr3f(hsum(sumB), hsum(sumG), hsum(sumR));
        }
#endif
        return out;
    }

    inline bgr3f shade(size_t i, const vec3 P, const vec3 D, const float u, const float v, Random& random) const {
        const vec3 T = (1-u-v) * vec3(scene.TX0[i], scene.TY0[i], scene.TZ0[i]) +
                             u * vec3(scene.TX1[i], scene.TY1[i], scene.TZ1[i]) +
                             v * vec3(scene.TX2[i], scene.TY2[i], scene.TZ2[i]) ;
        const vec3 B = (1-u-v) * vec3(scene.BX0[i], scene.BY0[i], scene.BZ0[i]) +
                             u * vec3(scene.BX1[i], scene.BY1[i], scene.BZ1[i]) +
                             v * vec3(scene.BX2[i], scene.BY2[i], scene.BZ2[i]) ;
        const vec3 N = (1-u-v) * vec3(scene.NX0[i], scene.NY0[i], scene.NZ0[i]) +
                             u * vec3(scene.NX1[i], scene.NY1[i], scene.NZ1[i]) +
                             v * vec3(scene.NX2[i], scene.NY2[i], scene.NZ2[i]) ;
        return shade(i, P, D, T, B, N, random);
    }

    inline bgr3f raycast_shade(const vec3 O, const vec3 D, Random& random) const {
        float t, u, v;
        const size_t faceIndex = raycast(O, D, t, u, v);
        return t<inff ? shade(faceIndex, O+t*D, D, u, v, random) : 0;
    }
};
