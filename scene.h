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

generic T squareWave(T x, const T frequency = T(16)) {
    const T n = floor(frequency*x); // Integer
    const T m = T(1./2)*n; // Half integer
    return T(2)*(m-floor(m)); // 0 or 1, 2*fract(n/2) = n%2
}

#include "raster.h"

struct Scene {
    const vec3 viewpoint;
    struct Face { vec3 position[4]; float u[4], v[4]; Image8 image; };
    buffer<Face> faces;

    /// Shader for all surfaces
    struct Shader {
        // Shader specification (used by rasterizer)
        struct FaceAttributes { uint stride; const uint8* image; };
        static constexpr int V = 2; //sizeof(Face::attributes)/sizeof(Face::attributes[0]); // U,V
        static constexpr bool blend = false; // Disables unnecessary blending

        template<int C, Type T> inline Vec<T, C> shade(FaceAttributes, T, T[V]) const;
        template<Type T> inline Vec<T, 0> shade0(FaceAttributes, T, T[V]) const { return {}; }
#if 1
        inline Vec<float, 3> shade3(FaceAttributes face, float, float varying[V]) const {
            const float u = varying[0], v = varying[1];
            const float s = face.image[int(v)*face.stride+int(u)]; // Nearest // FIXME: bilinear
            return Vec<float, 3>{{s, s, s}};
        }
#else
        template<Type T> inline Vec<T, 3> shade3(FaceAttributes face, T, T varying[V]) const {
#if 1 // Checkerboard
            const T u = varying[0], v = varying[1];
            static T cellCount = T(1); //static T cellCount = T(16);
            const T n = floor(cellCount*u)+floor(cellCount*v); // Integer
            const T m = T(1./2)*n; // Half integer
            const T mod = T(2)*(m-floor(m)); // 0 or 1, 2*fract(n/2) = n%2
            return Vec<T, 3>{{mod, mod, mod}};
#else
            return Vec<T, 3>{{T(face.color.b), T(face.color.g), T(face.color.r)}};
#endif
        }
#endif
    } shader {};

    template<int C> struct Renderer {
        RenderPass<Scene::Shader> pass; // Face bins
        RenderTarget<C> target; // Sample tiles
        Renderer(const Scene& scene) : pass(scene.shader) {}
    };

    template<Type... Args> void render(Renderer<sizeof...(Args)>& renderer, mat4 M, float clear[/*sizeof...(Args)*/], const ImageH& Z, const Args&... targets_) {
        const ImageH targets[sizeof...(Args)] { unsafeShare(targets_)... }; // Zero-length arrays are not permitted in C++
        uint2 size = (sizeof...(Args) ? targets[0] : Z).size;
        renderer.target.setup(int2(size), 1, clear); // Needs to be setup before pass
        renderer.pass.setup(renderer.target, ref<Face>(faces).size * 2 /* 1 quad face = 2 triangles */ ); // Clears bins face counter
        mat4 NDC;
        NDC.scale(vec3(vec2(size*4u)/2.f, 1)); // 0, 2 -> subsample size // *4u // MSAA->4x
        NDC.translate(vec3(vec2(1), 0.f)); // -1, 1 -> 0, 2
        M = NDC * M;
        for(const Face& face: faces) {
            vec4 a = M*vec4(face.position[0],1), b = M*vec4(face.position[1],1), c = M*vec4(face.position[2],1), d = M*vec4(face.position[3],1);
            if(cross((b/b.w-a/a.w).xyz(),(c/c.w-a/a.w).xyz()).z <= 0) continue; // Backward face culling
            renderer.pass.submit(a,b,c, (vec3[]){vec3(face.u[0],face.u[1],face.u[2]),vec3(face.v[0],face.v[1],face.v[2])}, {face.image.stride, face.image.data});
            renderer.pass.submit(a,c,d, (vec3[]){vec3(face.u[0],face.u[2],face.u[3]),vec3(face.v[0],face.v[2],face.v[3])}, {face.image.stride, face.image.data});
        }
        renderer.pass.render(renderer.target);
        renderer.target.resolve(Z, targets);
    }
};

// Explicit full function template specialization
template <> inline Vec<float, 0> Scene::Shader::shade<0, float>(FaceAttributes face, float z, float varying[V]) const { return shade0<float>(face, z, varying); }
template <> inline Vec<v16sf, 0> Scene::Shader::shade<0, v16sf>(FaceAttributes face, v16sf z, v16sf varying[V]) const { return shade0<v16sf>(face, z, varying); }
//template <> inline Vec<float, 3> Scene::Shader::shade<3, float>(FaceAttributes face, float z, float varying[V]) const { return shade3<float>(face, z, varying); }
//template <> inline Vec<v16sf, 3> Scene::Shader::shade<3, v16sf>(FaceAttributes face, v16sf z, v16sf varying[V]) const { return shade3<v16sf>(face, z, varying); }
template <> inline Vec<float, 3> Scene::Shader::shade<3, float>(FaceAttributes face, float z, float varying[V]) const { return shade3(face, z, varying); }
template <> inline Vec<v16sf, 3> Scene::Shader::shade<3, v16sf>(FaceAttributes face, v16sf z, v16sf varying[V]) const {
    Vec<v16sf, 3> Y;
    for(uint i: range(16)) {
        float x[V];
        for(uint v: range(V)) x[v] = varying[v][i];
        Vec<float, 3> y = shade3(face, z[i], x);
        for(uint c: range(3)) Y._[c][i] = y._[c];
    }
    return Y;
}

Scene parseScene(ref<byte> scene);
