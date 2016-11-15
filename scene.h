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
    v8sf Q1x, Q1y, Q1z; cross(T1x, T1y, T1z, e23x, e23y, e23z, Q1x, Q1y, Q1z);
    const v8sf b1 = dot(Dx, Dy, Dz, Q1x, Q1y, Q1z);

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
        float reflect, refract; vec3 N; // Render (RaycastShader)
        const half* BGR; uint2 size; // Display (TextureShader)
    };
    buffer<float> X[4], Y[4], Z[4]; // Quadrilaterals vertices world space positions XYZ coordinates
    //buffer<float> a11, b11;
    buffer<float> B, G, R; // Face color attributes
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
            const v8sf Dx = *(v8sf*)(X[3].data+i);
            const v8sf Dy = *(v8sf*)(Y[3].data+i);
            const v8sf Dz = *(v8sf*)(Z[3].data+i);
            const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Dx,Dy,Dz, Ox,Oy,Oz, dx,dy,dz);
            v8sf hmin = ::hmin(t);
            const float hmin0 = hmin[0];
            if(hmin0 >= value) continue;
            value = hmin0;
            index = i + ::indexOfEqual(t, hmin);
        }
        return index;
    }
    inline size_t raycast_reverseWinding(vec3 O, vec3 d, float* T=0) const {
        assert(faces.size < faces.capacity && align(8, faces.size)==faces.capacity);
        float value = inff; size_t index = faces.size;
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
            const v8sf Dx = *(v8sf*)(X[0].data+i);
            const v8sf Dy = *(v8sf*)(Y[0].data+i);
            const v8sf Dz = *(v8sf*)(Z[0].data+i);
            const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Dx,Dy,Dz, Ox,Oy,Oz, dx,dy,dz);
            v8sf hmin = ::hmin(t);
            const float hmin0 = hmin[0];
            if(hmin0 >= value) continue;
            value = hmin0;
            index = i + ::indexOfEqual(t, hmin);
        }
        if(T) *T=value;
        return index;
    }
#if 1
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
            const float Dx = X[3][i];
            const float Dy = Y[3][i];
            const float Dz = Z[3][i];
            const v8sf t = ::intersect(float8(Ax),float8(Ay),float8(Az), float8(Bx),float8(By),float8(Bz), float8(Cx),float8(Cy),float8(Cz), float8(Dx),float8(Dy),float8(Dz),
                                        Ox,Oy,Oz, dx,dy,dz);
            index = blend(index, i, t < value);
            value = ::min(value, t);
        }
        return index;
    }
#endif

    struct NoShader {
        static constexpr int C = 0;
        static constexpr int V = 0;
        typedef uint FaceAttributes;
        inline Vec<float, 0> shade(FaceAttributes, float, float[V]) const { return {}; }
        inline Vec<v16sf, C> shade(FaceAttributes, v16sf, v16sf[V], v16si) const { return {}; }
    };

    struct CheckerboardShader {
        static constexpr int C = 3;
        static constexpr int V = 2;
        typedef uint FaceAttributes;

        inline Vec<float, 3> shade(FaceAttributes, float, float varying[2]) const { return shade(varying); }
        inline Vec<v16sf, 3> shade(FaceAttributes, v16sf, v16sf varying[2], v16si) const { return shade(varying); }

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

        inline Vec<v16sf, C> shade(FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const {
            Vec<v16sf, C> Y;
            for(uint i: range(16)) {
                if(!mask[i]) continue;
                float x[V];
                for(uint v: range(V)) x[v] = varying[v][i];
                Vec<float, C> y = ((D*)this)->shade(face, z[i], x);
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
                attributes.Wts = {(1-fract(t))*(1-fract(s)), (1-fract(t))*fract(s), fract(t)*(1-fract(s)), fract(t)*fract(s)};
            }
        }

        inline Vec<v16sf, C> shade(FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const { return Shader::shade(face, z, varying, mask); }
        inline Vec<float, 3> shade(FaceAttributes index, float, float varying[V]) const {
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

    struct RaycastShader : Shader<3, 5, RaycastShader> {
        typedef uint FaceAttributes;

        const Scene& scene;
        vec3 viewpoint; // scene.viewpoint + (s, t)

        RaycastShader(const Scene& scene) : scene(scene) {}

        vec3 refract(const float r, const vec3 N, const vec3 D) const {
            const float c = -dot(N, D);
            return r*D + (r*c - sqrt(1-sq(r)*(1-sq(c))))*N;
        }

        inline Vec<v16sf, C> shade(FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const { return Shader::shade(face, z, varying, mask); }
        inline Vec<float, 3> shade(FaceAttributes index, float, float unused varying[V]) const {
            const Scene::Face& face = scene.faces[index];
            if(0) {}
#if 0
            else if(face.reflect) {
             const vec3 O = vec3(varying[2], varying[3], varying[4]);
             const vec3 D = normalize(O-viewpoint);
             const vec3 R = D - 2*dot(face.N, D)*face.N;
             bgr3f color = scene.raycast(O, normalize(R));
             return Vec<float, 3>{{color.b, color.g/2, color.r/2}};
            }
#endif
#if 1
            else if(face.refract) {
                const vec3 O = vec3(varying[2], varying[3], varying[4]);
                const vec3 D = normalize(O-viewpoint);
                const float n1 = 1, n2 = 1.3;
                const vec3 R = normalize(refract(n1/n2, normalize(face.N), D));
                float backT;
                size_t back = scene.raycast_reverseWinding(O, R, &backT);
                if(back == scene.faces.size) return {}; // FIXME
                const vec3 backO = O+backT*R;
                //const bgr3f color = (backO-scene.min)/(scene.max-scene.min);
                //const bgr3f color = (vec3(1)+normalize(face.N))/2.f;
                //return Vec<float, 3>{{color.b, color.g, color.r}};
                const vec3 backR = refract(n2/n1, normalize(-scene.faces[back].N), R);
                size_t index = scene.raycast(backO, backR);
                return Vec<float, 3>{{scene.B[index],scene.G[index],scene.R[index]}};
            }
#endif
            else {
                return Vec<float, 3>{{scene.B[index],scene.G[index],scene.R[index]}};
            }
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
            const vec3 D (X[3][faceIndex], Y[3][faceIndex], Z[3][faceIndex]);

            const vec4 a = M*vec4(A,1), b = M*vec4(B,1), c = M*vec4(C,1), d = M*vec4(D,1);
            if(cross((b/b.w-a/a.w).xyz(),(c/c.w-a/a.w).xyz()).z >= 0) continue; // Backward face culling

            const Face& face = faces[faceIndex];
            renderer.pass.submit(a,b,c, (vec3[]){vec3(face.u[0],face.u[1],face.u[2]),
                                                 vec3(face.v[0],face.v[1],face.v[2]),
                                                 vec3(A.x,B.x,C.x),
                                                 vec3(A.y,B.y,C.y),
                                                 vec3(A.z,B.z,C.z)}, faceIndex);
            renderer.pass.submit(a,c,d, (vec3[]){vec3(face.u[0],face.u[2],face.u[3]),
                                                 vec3(face.v[0],face.v[2],face.v[3]),
                                                 vec3(A.x,C.x,D.x),
                                                 vec3(A.y,C.y,D.y),
                                                 vec3(A.z,C.z,D.z)}, faceIndex);
        }
        renderer.pass.render(renderer.target);
        renderer.target.resolve(depth, targets);
    }
};

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
    if(existsFile(name+".obj")) return name+".obj";
    error("No such file", name);
}
