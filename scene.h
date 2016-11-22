#pragma once
#include "matrix.h"
#include "parallel.h"
#include "mwc.h"

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

#include "time.h"

struct Scene {
    struct Face {
        // Vertex attributes
        float u[3];
        float v[3];
        vec3 T[3], B[3], N[3];
        // Face attributes
        float reflect, refract, gloss; // Render (RaycastShader)
        const half* BGR; uint2 size; // Display (TextureShader)
    };
    buffer<float> X[3], Y[3], Z[3]; // Quadrilaterals vertices world space positions XYZ coordinates
    buffer<float> emittanceB, emittanceG, emittanceR; // Face color attributes
    buffer<float> reflectanceB, reflectanceG, reflectanceR; // 1/PI
    buffer<Face> faces;

    vec3 min, max;
    float scale, near, far;

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

    inline v8si raycast(const v8sf Ox, const v8sf Oy, const v8sf Oz, const v8sf dx, const v8sf dy, const v8sf dz) const {
        v8sf value = float8(inff); v8si index = intX(faces.size);
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

    inline v8si raycast(const v8sf Ox, const v8sf Oy, const v8sf Oz, const v8sf dx, const v8sf dy, const v8sf dz, v8sf& minT, v8sf& u, v8sf& v) const {
        minT = inff; v8si index = intX(faces.size);
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

    static inline vec3 refract(const float r, const vec3 N, const vec3 D) {
        const float c = -dot(N, D);
        return r*D + (r*c - sqrt(1-sq(r)*(1-sq(c))))*N;
    }

    // Uniformly distributed points on sphere (Marsaglia)
    Vec<v8sf, 3> sphere(Random& random) const {
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
    Vec<v8sf, 3> hemisphere(Random& random, vec3 N) const {
        const Vec<v8sf, 3> S = sphere(random);
        const v8si negate = (N.x*S._[0] + N.y*S._[1] + N.z*S._[2]) < 0;
        return {{blend(S._[0], -S._[0], negate),
                        blend(S._[1], -S._[1], negate),
                        blend(S._[2], -S._[2], negate)}};
    }
    Vec<v8sf, 3> cosine(Random& random) const {
        const v8sf ξ1 = random();
        const v8sf ξ2 = random();
        const v8sf cosθ = sqrt(1-ξ1);
        const v8sf sinθ = sqrt(ξ1);
        const v8sf φ = 2*PI*ξ2;
        const Vec<v8sf, 2> cossinφ = cossin(φ);
        return {{sinθ * cossinφ._[0], sinθ * cossinφ._[1], cosθ}};
    }

    enum Bounce { Direct=1, Diffuse, Specular, Max };
    typedef Bounce Path[3];
    typedef uint64 Timers[Max*Max*Max];
    bgr3f shade(size_t faceIndex, const vec3 P, const vec3 D, const vec3 T, const vec3 B, const vec3 N, Random& random, const int bounce, Path path, uint64* const timers, const size_t stride /*Max^bounce*/) const {
        const uint64 start = readCycleCounter();
        const Scene::Face& face = faces[faceIndex];
        bgr3f out = bgr3f(emittanceB[faceIndex], emittanceG[faceIndex], emittanceR[faceIndex]);
        const bgr3f BRDF = bgr3f(reflectanceB[faceIndex],reflectanceG[faceIndex],reflectanceR[faceIndex]);
        if(face.reflect) {
            if(bounce > 0) return 0; // +S
            //if(bounce > 1) return 0; // -SDS
            path[bounce] = Specular; // S
#if RT
            static constexpr int iterations = 1;
#else
            static constexpr int iterations = 64;
#endif
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
                v8si lightRayFaceIndex = raycast(P.x,P.y,P.z, Rx,Ry,Rz, t,u,v);
                for(uint k: range(8)) {
                    vec3 R = vec3(Rx[k],Ry[k],Rz[k]);
                    out += (1.f/(iterations*8)) * BRDF * shade(lightRayFaceIndex[k], P+t[k]*R, R, u[k], v[k], random, bounce+1, path, timers+Specular*stride, stride*Max);
                }
            }
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
        else { // Diffuse
            // Direct lighting
            v8sf sumB=0, sumG=0, sumR=0;
#if RT
            static constexpr int directIterations = 1;
            static constexpr int indirectIterations = 1;
#else
            const int directIterations = bounce < 1 ? 2048 : 1;
            constexpr int indirectIterations = 1;
#endif
            const float scale = 1.f/(directIterations*8+indirectIterations*8);
            for(uint unused i: range(directIterations)) {
                const Vec<v8sf, 3> l = cosine(random);
                const v8sf Lx = T.x * l._[0] + B.x * l._[1] + N.x * l._[2];
                const v8sf Ly = T.y * l._[0] + B.y * l._[1] + N.y * l._[2];
                const v8sf Lz = T.z * l._[0] + B.z * l._[1] + N.z * l._[2];
                v8si lightRayFaceIndex = raycast(P.x,P.y,P.z, Lx,Ly,Lz);
                sumB += gather(emittanceB.data, lightRayFaceIndex);
                sumG += gather(emittanceG.data, lightRayFaceIndex);
                sumR += gather(emittanceR.data, lightRayFaceIndex);
            }
            out.b += scale * BRDF.b * hsum(sumB);
            out.g += scale * BRDF.g * hsum(sumG);
            out.r += scale * BRDF.r * hsum(sumR);
            if(bounce < 1) { // Indirect diffuse lighting
                path[bounce] = Diffuse;
                bgr3f sum;
                for(uint unused i: range(indirectIterations)) {
                    const Vec<v8sf, 3> l = cosine(random);
                    const v8sf Lx = T.x * l._[0] + B.x * l._[1] + N.x * l._[2];
                    const v8sf Ly = T.y * l._[0] + B.y * l._[1] + N.y * l._[2];
                    const v8sf Lz = T.z * l._[0] + B.z * l._[1] + N.z * l._[2];
                    v8sf t,u,v;
                    v8si lightRayFaceIndex = raycast(P.x,P.y,P.z, Lx,Ly,Lz, t,u,v);
                    for(uint k: range(8)) {
                        vec3 L = vec3(Lx[k],Ly[k],Lz[k]);
                        sum += shade(lightRayFaceIndex[k], P+t[k]*L, L, u[k], v[k], random, bounce+1, path, timers+Diffuse*stride, stride*Max);
                    }
                }
                out += scale * BRDF * sum;
            } else path[bounce] = Direct;
        }
        timers[path[bounce]*stride] += readCycleCounter()-start;
        return out;
    }

    inline bgr3f shade(size_t faceIndex, const vec3 P, const vec3 D, const float u, const float v, Random& random, const uint bounce, Path path, uint64* const timers, const size_t stride /*Max^bounce*/) const {
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

    inline bgr3f raycast_shade(const vec3 O, const vec3 D, Random& random, const uint bounce, Path path, uint64* const timers, const size_t stride) const;

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
            const half* BGR[3];
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
                attributes.sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                           size3/2,   (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
                if(sSize == 1 || tSize ==1) // Prevents OOB
                    attributes.sample4D = {    0,           size1/2,         0,       size1/2,
                                               0,           size1/2,         0,       size1/2};
                attributes.Wts = {(1-fract(t))*(1-fract(s)), (1-fract(t))*fract(s), fract(t)*(1-fract(s)), fract(t)*fract(s)};
            }
        }

        inline Vec<v16sf, C> shade(const uint id, FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const { return Shader::shade(id, face, z, varying, mask); }
        inline Vec<float, 3> shade(const uint, FaceAttributes index, float, float varying[V]) const {
            const float u = varying[0], v = varying[1];
            const int vIndex = v, uIndex = u; // Floor
            const Face& face = faces[index];
            const size_t base = 2*face.sample4D[1]*vIndex + uIndex;
            const v16sf B = toFloat((v16hf)gather((float*)(face.BGR[0]+base), face.sample4D));
            const v16sf G = toFloat((v16hf)gather((float*)(face.BGR[1]+base), face.sample4D));
            const v16sf R = toFloat((v16hf)gather((float*)(face.BGR[2]+base), face.sample4D));
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

        const Scene& scene;
        vec3 viewpoint; // (s, t)
        buffer<Random> randoms;

        RaycastShader(const Scene& scene) : scene(scene) {
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

inline bgr3f Scene::raycast_shade(const vec3 O, const vec3 D, Random& random, const uint bounce, Path path, uint64* const timers, const size_t stride) const {
    float t, u, v;
    const size_t faceIndex = raycast(O, D, t, u, v);
    return shade(faceIndex, O+t*D, D, u, v, random, bounce, path, timers, stride);
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
