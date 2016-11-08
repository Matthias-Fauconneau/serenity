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

inline bool intersect(vec3 A, vec3 B, vec3 C, vec3 O, vec3 d, float& t, float& u, float& v) { // "Fast, Minimum Storage Ray/Triangle Intersection"
    vec3 e2 = C - A;
    vec3 e1 = B - A;
    vec3 P = cross(d, e2);
    float det = dot(e1, P);
    if(det < 0) return false;
    vec3 T = O - A;
    u = dot(T, P);
    if(u < 0 || u > det) return false;
    vec3 Q = cross(T, e1);
    v = dot(d, Q);
    if(v < 0 || u + v > det) return false;
    t = dot(e2, Q);
    if(t < 0) return false;
    t /= det;
    u /= det;
    v /= det;
    return true;
}

// "Efficient Ray-Quadrilateral Intersection Test"
static bool intersect(vec3 v00, vec3 v10, vec3 v11, vec3 v01, vec3 O, vec3 d, float& t /*, float a11, float b11, float& u, float& v*/) {
    const vec3 e03 = v01 - v00;
    const vec3 e01 = v10 - v00;
    const vec3 P = cross(d, e03);
    float det = dot(e01, P);
    if(det < 0) return false;
    const vec3 T0 = O - v00;
    float a = dot(T0, P);
    if(a < 0 || a > det) return false;
    const vec3 Q0 = cross(T0, e01);
    float b = dot(d, Q0);
    if(b < 0 || b > det) return false; // FIXME: assert v11 within parallelogram and reorder otherwise
    // in parallelogram
    if(a + b > det) { // not in T
        const vec3 e23 = v01 - v11;
        const vec3 e21 = v10 - v11;
        const vec3 P1 = cross(d, e21);
        const float det1 = dot(e23, P1);
        if(det1 < 0) return false;
        const vec3 T1 = O - v11;
        const float a1 = dot(T1, P1);
        if(a1 < 0) return false;
        const vec3 Q1 = cross(T1, e23);
        const float b1 = dot(d, Q1);
        if(b1 < 0) return false;
    }

    t = dot(e03, Q0);
    if(t < 0) return false;
    t /= det;

#if 0
    /**/ if(abs(a11-1) < 0) {
        u = a / det;
        if(abs(b11-1) < 0) v = b / det;
        else v = b / (det*(u*(b11-1)+1));
    }
    else if(abs(b11-1) < 0) {
        v = b / det;
        u = a / (det*(v*(a11-1)+1));
    } else {
        const float A = -(b11-1);
        const float B = a*(b11-1) - b*(a11-1) - 1;
        const float C = a;
        const float delta = B*B - 4*A*C;
        const float Q = (-1.f/2)*(B+(B>0?1:-1)*sqrt(delta));
        u = Q/A;
        if(u < 0 || u > 1) u = C/Q;
        v = b / (det*u*(b11-1)+1);
    }
#endif
    return true;
}

#if 1
static bool intersect(v8sf x00, v8sf y00, v8sf z00,
                      v8sf x10, v8sf y10, v8sf z10,
                      v8sf x11, v8sf y11, v8sf z1,
                      v8sf x01, v8sf y01, v8sf z01,
                      v8sf a11, v8sf b11,
                      vec3 O, vec3 d, v8sf& t/*, v8sf& u, v8sf& v*/) { // "Efficient Ray-Quadrilateral Intersection Test"
    const vec3 e03 = v01 - v00;
    const vec3 e01 = v10 - v00;
    const vec3 P = cross(d, e03);
    float det = dot(e01, P);
    if(det < 0) return false;
    const vec3 T0 = O - v00;
    float a = dot(T0, P);
    if(a < 0 || a > det) return false;
    const vec3 Q0 = cross(T0, e01);
    float b = dot(d, Q0);
    if(b < 0 || b > det) return false; // FIXME: assert v11 within parallelogram and reorder otherwise
    // in parallelogram
    if(a + b > det) { // not in T
        const vec3 e23 = v01 - v11;
        const vec3 e21 = v10 - v11;
        const vec3 P1 = cross(d, e21);
        const float det1 = dot(e23, P1);
        if(det1 < 0) return false;
        const vec3 T1 = O - v11;
        const float a1 = dot(T1, P1);
        if(a1 < 0) return false;
        const vec3 Q1 = cross(T1, e23);
        const float b1 = dot(d, Q1);
        if(b1 < 0) return false;
    }

    t = dot(e03, Q0);
    if(t < 0) return false;
    t /= det;

    /**/ if(abs(a11-1) < 0) {
        u = a / det;
        if(abs(b11-1) < 0) v = b / det;
        else v = b / (det*(u*(b11-1)+1));
    }
    else if(abs(b11-1) < 0) {
        v = b / det;
        u = a / (det*(v*(a11-1)+1));
    } else {
        const float A = -(b11-1);
        const float B = a*(b11-1) - b*(a11-1) - 1;
        const float C = a;
        const float delta = B*B - 4*A*C;
        const float Q = (-1.f/2)*(B+(B>0?1:-1)*sqrt(delta));
        u = Q/A;
        if(u < 0 || u > 1) u = C/Q;
        v = b / (det*u*(b11-1)+1);
    }
    return true;
}
#endif

struct Scene {
    vec3 viewpoint;
    struct Face {
        float u[4], v[4]; // Vertex attributes
        struct Attributes { // Face attributes
            float reflect; bgr3f color; vec3 N; // Render (RaycastShader)
            const half* BGR; uint2 size; // Display (TextureShader)
        } attributes;
    };
    buffer<float> X[4], Y[4], Z[4], a11, b11; // Quadrilaterals vertices world space positions XYZ coordinates
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
        float nearestZ = inff; bgr3f color (0, 0, 0);
        for(size_t i: range(faces.size)) {
            const vec3 A (X[0][i], Y[0][i], Z[0][i]);
            const vec3 B (X[1][i], Y[1][i], Z[1][i]);
            const vec3 C (X[2][i], Y[2][i], Z[2][i]);
            const vec3 D (X[3][i], Y[3][i], Z[3][i]);
            //const float a11 = this->a11[i], b11 = this->b11[i];
            float t;//, u, v;
            if(!::intersect(A, B, C, D, O, d, t/*a11, b11, u, v*/)) continue;
            float z = t*d.z;
            if(z > nearestZ) continue;
            nearestZ = z;
            color = faces[i].attributes.color;
        }
        return color;
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
