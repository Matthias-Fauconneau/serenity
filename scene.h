#pragma once
#include "matrix.h"

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

// "Efficient Ray-Quadrilateral Intersection Test"
static inline float intersect(vec3 v00, vec3 v10, vec3 v11, vec3 v01, vec3 O, vec3 d) {
    const vec3 e03 = v01 - v00;
    const vec3 e01 = v10 - v00;
    const vec3 P = cross(d, e03);
    float det = dot(e01, P);
    if(det <= 0) return inff;
    const vec3 T0 = O - v00;
    float a = dot(T0, P);
    if(a < 0 || a > det) return inff;
    const vec3 Q0 = cross(T0, e01);
    float b = dot(d, Q0);
    if(b < 0 || b > det) return inff; // FIXME: assert v11 within parallelogram and reorder otherwise
    // in parallelogram
    if(a + b > det) { // not in T
        const vec3 e23 = v01 - v11;
        const vec3 e21 = v10 - v11;
        const vec3 P1 = cross(d, e21);
        const float det1 = dot(e23, P1);
        if(det1 < 0) return inff;
        const vec3 T1 = O - v11;
        const float a1 = dot(T1, P1);
        if(a1 < 0) return inff;
        const vec3 Q1 = cross(T1, e23);
        const float b1 = dot(d, Q1);
        if(b1 < 0) return inff;
    }

    const float t = dot(e03, Q0);
    return t > 0 ? t / det : inff;
}

// "Efficient Ray-Quadrilateral Intersection Test"
static inline v8sf intersect(const v8sf x00, const v8sf y00, const v8sf z00,
                             const v8sf x10, const v8sf y10, const v8sf z10,
                             const v8sf x11, const v8sf y11, const v8sf z11,
                             const v8sf x01, const v8sf y01, const v8sf z01,
                             const v8sf  Ox, const v8sf  Oy, const v8sf  Oz,
                             const v8sf  Dx, const v8sf  Dy, const v8sf  Dz) {
    const v8sf e03x = x01 - x00;
    const v8sf e03y = y01 - y00;
    const v8sf e03z = z01 - z00;
    v8sf P0x, P0y, P0z; cross(Dx, Dy, Dz, e03x, e03y, e03z, P0x, P0y, P0z);
    const v8sf T0x = Ox - x00;
    const v8sf T0y = Oy - y00;
    const v8sf T0z = Oz - z00;
    const v8sf a0 = dot(T0x, T0y, T0z, P0x, P0y, P0z);
    const v8sf e01x = x10 - x00;
    const v8sf e01y = y10 - y00;
    const v8sf e01z = z10 - z00;
    const v8sf det0 = dot(e01x, e01y, e01z, P0x, P0y, P0z);
    v8sf Q0x, Q0y, Q0z; cross(T0x, T0y, T0z, e01x, e01y, e01z, Q0x, Q0y, Q0z);
    const v8sf b0 = dot(Dx, Dy, Dz, Q0x, Q0y, Q0z);
    const v8sf t = dot(e03x, e03y, e03z, Q0x, Q0y, Q0z) / det0;

    const v8sf e21x = x10 - x11;
    const v8sf e21y = y10 - y11;
    const v8sf e21z = z10 - z11;
    v8sf P1x, P1y, P1z; cross(Dx, Dy, Dz, e21x, e21y, e21z, P1x, P1y, P1z);
    const v8sf T1x = Ox - x11;
    const v8sf T1y = Oy - y11;
    const v8sf T1z = Oz - z11;
    const v8sf a1 = dot(T1x, T1y, T1z, P1x, P1y, P1z);
    const v8sf e23x = x01 - x11;
    const v8sf e23y = y01 - y11;
    const v8sf e23z = z01 - z11;
    //const v8sf det1 = dot(e23x, e23y, e23z, P1x, P1y, P1z); // FIXME
    v8sf Q1x, Q1y, Q1z; cross(T1x, T1y, T1z, e23x, e23y, e23z, Q1x, Q1y, Q1z);
    const v8sf b1 = dot(Dx, Dy, Dz, Q1x, Q1y, Q1z);

    //return blend(float8(inff), t, det0 >= _0f && a0 >= _0f && b0 >= _0f && a1 >= _0f && b1 >= _0f && t >= _0f && a0 <= det0 && b0 <= det0 && det1 >= _0f && a1 <= det1 && b1 <= det1);
    return blend(float8(inff), t, det0 > _0f && a0 >= _0f && b0 >= _0f && a1 >= _0f && b1 >= _0f && t > _0f);
}

inline v8sf hmin(const v8sf x) {
    const v8sf v0 = __builtin_ia32_minps256(x, _mm256_alignr_epi8(x, x, 4));
    const v8sf v1 = __builtin_ia32_minps256(v0, _mm256_alignr_epi8(v0, v0, 8));
    return __builtin_ia32_minps256(v1, _mm256_permute2x128_si256(v1, v1, 0x01));
}

inline uint indexOfEqual(const v8sf x, const v8sf y) {
    return __builtin_ctz(::mask(x == y));
}

struct Scene {
    vec3 viewpoint;
    struct Face {
        float u[4], v[4]; // Vertex attributes
        struct Attributes { // Face attributes
            float reflect; bgr3f color; vec3 N; // Render (RaycastShader)
            const half* BGR; uint2 size; // Display (TextureShader)
        } attributes;
    };
    buffer<float> X[4], Y[4], Z[4]; // Quadrilaterals vertices world space positions XYZ coordinates
    buffer<float> a11, b11;
    buffer<Face> faces;

    vec3 min, max;
    float scale, near, far;

    void fit() {
        // Fits scene
        min = inff, max = -inff;
        for(size_t i: range(4)) {
            min = ::min(min, vec3(::min(X[i]), ::min(Y[i]), ::min(Z[i])));
            max = ::max(max, vec3(::max(X[i]), ::max(Y[i]), ::max(Z[i])));
        }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        scale = 2./::max(max.x-min.x, max.y-min.y);
        near = scale*(-viewpoint.z+min.z);
        far = scale*(-viewpoint.z+max.z);
    }

    inline bgr3f raycast(vec3 O, vec3 d) const {
        float value = inff; size_t index = invalid;
        assert_(align(8, faces.size)==faces.capacity);
#if 0
        for(size_t i=0; i<faces.size; i++) {
            const float Ax = X[0][i];
            const float Ay = Y[0][i];
            const float Az = Z[0][i];
            const float Bx = X[1][i];
            const float By = Y[1][i];
            const float Bz = Z[1][i];
            const float Cx = X[2][i];
            const float Cy = Y[2][i];
            const float Cz = Z[2][i];
            const float Dx = X[3][i];
            const float Dy = Y[3][i];
            const float Dz = Z[3][i];
            const float t = ::intersect(vec3(Ax,Ay,Az),vec3(Bx,By,Bz),vec3(Cx,Cy,Cz),vec3(Dx,Dy,Dz), O, d);
            if(t >= value) continue;
            value = t;
            index = i;
        }
#else
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
            const v8sf Dx = *(v8sf*)(X[3].data+i);
            const v8sf Dy = *(v8sf*)(Y[3].data+i);
            const v8sf Dz = *(v8sf*)(Z[3].data+i);
            const v8sf t = ::intersect(Ax, Ay, Az, Bx, By, Bz, Cx, Cy, Cz, Dx, Dy, Dz, Ox, Oy, Oz, dx, dy, dz);
            v8sf hmin = ::hmin(t);
            const float hmin0 = hmin[0];
            if(hmin0 >= value) continue;
            value = hmin0;
            index = i + ::indexOfEqual(t, hmin);
        }
#endif
        return index==invalid ? bgr3f(0) : faces[index].attributes.color;
    }

    struct TextureShader {
        static constexpr int V = 2;
        typedef Scene::Face::Attributes FaceAttributes;

        uint sSize = 0, tSize = 0;
        float s = 0, t = 0; // 0 .. (s,t)Size
        uint sIndex = 0, tIndex = 0; // floor (s, t)

        TextureShader(const Scene&) {}

        template<int C> inline Vec<float, C> shade(FaceAttributes, float, float[V]) const;
        template<int C> inline Vec<v16sf, C> shade(FaceAttributes, v16sf, v16sf[V], v16si) const;

        template<Type T> inline Vec<T, 0> shade0(FaceAttributes, T, T[V]) const { return {}; }
        inline Vec<float, 3> shade3(FaceAttributes face, float, float varying[V]) const {
            const int size1 = face.size.x;
            const int size2 = face.size.y*size1;
            const int size3 = sSize      *size2;
            // FIXME: face attribute (+base)
            const v8si sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                 size3/2,   (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
            float u = clamp(0.f, varying[0], face.size.x-1-0x1p-16f);
            float v = clamp(0.f, varying[1], face.size.y-1-0x1p-16f);
            const int vIndex = v, uIndex = u; // Floor
            const size_t base = (size_t)tIndex*size3 + sIndex*size2 + vIndex*size1 + uIndex;
            const size_t size4 = tSize*size3;
            const v16sf B = toFloat((v16hf)gather((float*)(face.BGR+0*size4+base), sample4D));
            const v16sf G = toFloat((v16hf)gather((float*)(face.BGR+1*size4+base), sample4D));
            const v16sf R = toFloat((v16hf)gather((float*)(face.BGR+2*size4+base), sample4D));
            const v4sf x = {t, s, v, u}; // tsvu
            const v8sf X = __builtin_shufflevector(x, x, 0,1,2,3, 0,1,2,3);
            static const v8sf _00001111f = {0,0,0,0,1,1,1,1};
            const v8sf w_1mw = abs(X - floor(X) - _00001111f); // fract(x), 1-fract(x)
            const v16sf w01 = shuffle(w_1mw, w_1mw, 4,4,4,4,4,4,4,4, 0,0,0,0,0,0,0,0)  // ttttttttTTTTTTTT
                            * shuffle(w_1mw, w_1mw, 5,5,5,5,1,1,1,1, 5,5,5,5,1,1,1,1)  // ssssSSSSssssSSSS
                            * shuffle(w_1mw, w_1mw, 6,6,2,2,6,6,2,2, 6,6,2,2,6,6,2,2)  // vvVVvvVVvvVVvvVV
                            * shuffle(w_1mw, w_1mw, 7,3,7,3,7,3,7,3, 7,3,7,3,7,3,7,3); // uUuUuUuUuUuUuUuU
            return Vec<float, 3>{{dot(w01, B), dot(w01, G), dot(w01, R)}};
        }
    };

    struct CheckerboardShader {
        typedef Scene::Face::Attributes FaceAttributes;
        static constexpr int V = 2;

        CheckerboardShader(const Scene&) {}

        template<int C> inline Vec<float, C> shade(FaceAttributes, float, float[V]) const;
        template<int C> inline Vec<v16sf, C> shade(FaceAttributes, v16sf, v16sf[V], v16si) const;

        template<Type T> inline Vec<T, 0> shade0(FaceAttributes, T, T[V]) const { return {}; }
        template<Type T> inline Vec<T, 3> shade3(FaceAttributes, T, T varying[V]) const {
            const T u = varying[0], v = varying[1];
            static T cellCount = T(1); //static T cellCount = T(16);
            const T n = floor(cellCount*u)+floor(cellCount*v); // Integer
            const T m = T(1./2)*n; // Half integer
            const T mod = T(2)*(m-floor(m)); // 0 or 1, 2*fract(n/2) = n%2
            return Vec<T, 3>{{mod, mod, mod}};
        }
    };

    struct RaycastShader {
        typedef Scene::Face::Attributes FaceAttributes;
        static constexpr int V = 5;

        const Scene& scene;
        vec3 viewpoint; // scene.viewpoint + (s, t)

        RaycastShader(const Scene& scene) : scene(scene) {}

        template<int C> inline Vec<float, C> shade(FaceAttributes, float, float[V]) const;
        template<int C> inline Vec<v16sf, C> shade(FaceAttributes, v16sf, v16sf[V], v16si) const;

        template<Type T> inline Vec<T, 0> shade0(FaceAttributes, T, T[V]) const { return {}; }
        inline Vec<float, 3> shade3(FaceAttributes face, float, float varying[V]) const {
            if(!face.reflect) return Vec<float, 3>{{face.color.b, face.color.g, face.color.r}};
            const vec3 O = vec3(varying[2], varying[3], varying[4]);
            const vec3 D = normalize(O-viewpoint);
            const vec3 R = D - 2*dot(face.N, D)*face.N;
            bgr3f color = scene.raycast(O, normalize(R));
            return Vec<float, 3>{{color.b, color.g/2, color.r/2}};
        }
    };

    template<Type Shader, int C> struct Renderer {
        Shader shader; // Instance holds any uniforms (currently none)
        RenderPass<Shader> pass; // Face bins
        RenderTarget<C> target; // Sample tiles
        Renderer(const Scene& scene) : shader(scene), pass(shader) {}
    };

    template<Type Shader, Type... Args>
    void render(Renderer<Shader, sizeof...(Args)>& renderer, mat4 M, float clear[/*sizeof...(Args)*/], const ImageH& depth, const Args&... targets_) {
        const ImageH targets[sizeof...(Args)] { unsafeShare(targets_)... }; // Zero-length arrays are not permitted in C++
        uint2 size = (sizeof...(Args) ? targets[0] : depth).size;
        renderer.target.setup(int2(size), 1, clear); // Needs to be setup before pass
        renderer.pass.setup(renderer.target, ref<Face>(faces).size * 2 /* 1 quad face = 2 triangles */ ); // Clears bins face counter
        mat4 NDC;
        NDC.scale(vec3(vec2(size*4u)/2.f, 1)); // 0, 2 -> subsample size // *4u // MSAA->4x
        NDC.translate(vec3(vec2(1), 0.f)); // -1, 1 -> 0, 2
        M = NDC * M;
        for(size_t i: range(faces.size)) {
            const vec3 A (X[0][i], Y[0][i], Z[0][i]);
            const vec3 B (X[1][i], Y[1][i], Z[1][i]);
            const vec3 C (X[2][i], Y[2][i], Z[2][i]);
            const vec3 D (X[3][i], Y[3][i], Z[3][i]);

            const vec4 a = M*vec4(A,1), b = M*vec4(B,1), c = M*vec4(C,1), d = M*vec4(D,1);
            if(cross((b/b.w-a/a.w).xyz(),(c/c.w-a/a.w).xyz()).z >= 0) continue; // Backward face culling

            const Face& face = faces[i];
            renderer.pass.submit(a,b,c, (vec3[]){vec3(face.u[0],face.u[1],face.u[2]),
                                                 vec3(face.v[0],face.v[1],face.v[2]),
                                                 vec3(A.x,B.x,C.x),
                                                 vec3(A.y,B.y,C.y),
                                                 vec3(A.z,B.z,C.z)}, face.attributes);
            renderer.pass.submit(a,c,d, (vec3[]){vec3(face.u[0],face.u[2],face.u[3]),
                                                 vec3(face.v[0],face.v[2],face.v[3]),
                                                 vec3(A.x,C.x,D.x),
                                                 vec3(A.y,C.y,D.y),
                                                 vec3(A.z,C.z,D.z)}, face.attributes);
        }
        renderer.pass.render(renderer.target);
        renderer.target.resolve(depth, targets);
    }
};

// Explicit full function template specialization
template <> inline Vec<float, 0> Scene::CheckerboardShader::shade<0>(FaceAttributes face, float z, float varying[V]) const { return shade0<float>(face, z, varying); }
template <> inline Vec<v16sf, 0> Scene::CheckerboardShader::shade<0>(FaceAttributes face, v16sf z, v16sf varying[V], v16si) const { return shade0<v16sf>(face, z, varying); }
template <> inline Vec<float, 3> Scene::CheckerboardShader::shade<3>(FaceAttributes face, float z, float varying[V]) const { return shade3<float>(face, z, varying); }
template <> inline Vec<v16sf, 3> Scene::CheckerboardShader::shade<3>(FaceAttributes face, v16sf z, v16sf varying[V], v16si) const { return shade3<v16sf>(face, z, varying); }

template <> inline Vec<float, 0> Scene::TextureShader::shade<0>(FaceAttributes face, float z, float varying[V]) const { return shade0<float>(face, z, varying); }
template <> inline Vec<v16sf, 0> Scene::TextureShader::shade<0>(FaceAttributes face, v16sf z, v16sf varying[V], v16si) const { return shade0<v16sf>(face, z, varying); }
template <> inline Vec<float, 3> Scene::TextureShader::shade<3>(FaceAttributes face, float z, float varying[V]) const { return shade3(face, z, varying); }
template <> inline Vec<v16sf, 3> Scene::TextureShader::shade<3>(FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const {
    Vec<v16sf, 3> Y;
    for(uint i: range(16)) {
        if(!mask[i]) continue;
        float x[V];
        for(uint v: range(V)) x[v] = varying[v][i];
        Vec<float, 3> y = shade3(face, z[i], x);
        for(uint c: range(3)) Y._[c][i] = y._[c];
    }
    return Y;
}

template <> inline Vec<float, 0> Scene::RaycastShader::shade<0>(FaceAttributes face, float z, float varying[V]) const { return shade0<float>(face, z, varying); }
template <> inline Vec<v16sf, 0> Scene::RaycastShader::shade<0>(FaceAttributes face, v16sf z, v16sf varying[V], v16si) const { return shade0<v16sf>(face, z, varying); }
template <> inline Vec<float, 3> Scene::RaycastShader::shade<3>(FaceAttributes face, float z, float varying[V]) const { return shade3(face, z, varying); }
// FIXME: duplicate definition with TextureShader
template <> inline Vec<v16sf, 3> Scene::RaycastShader::shade<3>(FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const {
    Vec<v16sf, 3> Y;
    for(uint i: range(16)) {
        if(!mask[i]) continue;
        float x[V];
        for(uint v: range(V)) x[v] = varying[v][i];
        Vec<float, 3> y = shade3(face, z[i], x);
        for(uint c: range(3)) Y._[c][i] = y._[c];
    }
    return Y;
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
    if(existsFile(name+".scene")) return name+".scene";
    if(existsFile(name+".ply")) return name+".ply";
    error("No such file", name);
}
