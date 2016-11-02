#pragma once
#include "matrix.h"

inline mat4 shearedPerspective(const float s, const float t, const float near, const float far) { // Sheared perspective (rectification)
    const float S = 2*s-1, T = 2*t-1; // [0,1] -> [-1, 1]
    const float left = (-1-S), right = (1-S);
    const float bottom = (-1-T), top = (1-T);
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
    M.translate(vec3(-S,-T,0));
    return M;
}

static bool intersect(vec3 A, vec3 B, vec3 C, vec3 O, vec3 d, float& t, float& u, float& v) { //from "Fast, Minimum Storage Ray/Triangle Intersection"
    vec3 edge1 = B - A;
    vec3 edge2 = C - A;
    vec3 pvec = cross(d, edge2);
    float det = dot(edge1, pvec);
    if(det < 0) return false;
    vec3 tvec = O - A;
    u = dot(tvec, pvec);
    if(u < 0 || u > det) return false;
    vec3 qvec = cross(tvec, edge1);
    v = dot(d, qvec);
    if(v < 0 || u + v > det) return false;
    t = dot(edge2, qvec);
    t /= det;
    u /= det;
    v /= det;
    return true;
}

generic T squareWave(T x, const T frequency = T(16)) {
    const T n = floor(frequency*x); // Integer
    const T m = T(1./2)*n; // Half integer
    return T(2)*(m-floor(m)); // 0 or 1, 2*fract(n/2) = n%2
}

#include "raster.h"

struct Scene {
    const vec3 viewpoint;
    struct Face { vec3 position[3]; vec3 attributes[2]; bgr3f color; };
    buffer<Face> faces;

    /// Shader for all surfaces
    struct Shader {
        // Shader specification (used by rasterizer)
        struct FaceAttributes { bgr3f color; };
        static constexpr int V = sizeof(Face::attributes)/sizeof(Face::attributes[0]); // U,V
        static constexpr bool blend = false; // Disables unnecessary blending

        template<int C, Type T> inline Vec<T, C> shade(FaceAttributes, T, T[V]) const;
        template<Type T> inline Vec<T, 0> shade0(FaceAttributes, T, T[V]) const { return {}; }
        template<Type T> inline Vec<T, 3> shade3(FaceAttributes face, T, T varying[V]) const {
#if 1 // Checkerboard
            const T u = varying[0], v = varying[1];
            static T cellCount = T(16);
            const T n = floor(cellCount*u)+floor(cellCount*v); // Integer
            const T m = T(1./2)*n; // Half integer
            const T mod = T(2)*(m-floor(m)); // 0 or 1, 2*fract(n/2) = n%2
            return Vec<T, 3>{{mod*T(face.color.b), mod*T(face.color.g), mod*T(face.color.r)}};
#else
            return Vec<T, 3>{{T(face.color.b), T(face.color.g), T(face.color.r)}};
#endif
        }
    } shader {};

    bgr3f raycast(vec3 O, vec3 d) const {
        float nearestZ = inff; bgr3f color (0, 0, 0);
        for(Face face: faces) {
            float t, u, v;
            if(!::intersect(face.position[0], face.position[1], face.position[2], O, d, t, u, v)) continue;
            float z = t*d.z;
            if(z > nearestZ) continue;
            nearestZ = z;
            color = face.color;
        }
        return color;
    }

    template<int C> struct Renderer {
        RenderPass<Scene::Shader> pass; // Face bins
        RenderTarget<C> target; // Sample tiles
        Renderer(const Scene& scene) : pass(scene.shader) {}
    };

    template<Type... Args> void render(Renderer<sizeof...(Args)>& renderer, mat4 M, float clear[/*sizeof...(Args)*/], const ImageH& Z, const Args&... targets_) {
        const ImageH targets[sizeof...(Args)] { unsafeShare(targets_)... }; // Zero-length arrays are not permitted in C++
        uint2 size = (sizeof...(Args) ? targets[0] : Z).size;
        renderer.target.setup(int2(size), 1, clear); // Needs to be setup before pass
        renderer.pass.setup(renderer.target, ref<Face>(faces).size); // Clears bins face counter
        mat4 NDC;
        NDC.scale(vec3(vec2(size*4u)/2.f, 1)); // 0, 2 -> subsample size // *4u // MSAA->4x
        NDC.translate(vec3(vec2(1), 0.f)); // -1, 1 -> 0, 2
        M = NDC * M;
        for(const Face& face: faces) {
            vec4 a = M*vec4(face.position[0],1), b = M*vec4(face.position[1],1), c = M*vec4(face.position[2],1);
            if(cross((b/b.w-a/a.w).xyz(),(c/c.w-a/a.w).xyz()).z <= 0) continue; // Backward face culling
            renderer.pass.submit(a,b,c, face.attributes, {face.color});
        }
        renderer.pass.render(renderer.target);
        renderer.target.resolve(Z, targets);
    }
};

// Explicit full function template specialization
template <> inline Vec<float, 0> Scene::Shader::shade<0, float>(FaceAttributes face, float z, float varying[V]) const { return shade0<float>(face, z, varying); }
template <> inline Vec<v16sf, 0> Scene::Shader::shade<0, v16sf>(FaceAttributes face, v16sf z, v16sf varying[V]) const { return shade0<v16sf>(face, z, varying); }
template <> inline Vec<float, 3> Scene::Shader::shade<3, float>(FaceAttributes face, float z, float varying[V]) const { return shade3<float>(face, z, varying); }
template <> inline Vec<v16sf, 3> Scene::Shader::shade<3, v16sf>(FaceAttributes face, v16sf z, v16sf varying[V]) const { return shade3<v16sf>(face, z, varying); }

Scene parseScene(ref<byte> scene);
