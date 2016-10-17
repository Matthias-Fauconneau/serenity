#pragma once
#include "raster.h"
#include "interface.h"

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

struct Scene {
    /// Shader for all surfaces
    struct Shader {
        // Shader specification (used by rasterizer)
        struct FaceAttributes { bgr3f color; };
        static constexpr int V = 3; // U,V,Z
        static constexpr bool blend = false; // Disables unnecessary blending

        template<int C> vecf<C> shade(FaceAttributes face, float z, float varying[V]) const;
        template<int C> vec16f<C> shade(FaceAttributes face, v16sf z, v16sf varying[V]) const;
    } shader;

    struct Face { vec3 position[3]; vec3 attributes[Shader::V]; bgr3f color; };
    Face faces[6*2]; // Cube

    Scene() {
        vec3 position[8];
        const float size = 1./2;
        for(int i: range(8)) position[i] = size * (2.f * vec3(i/4, (i/2)%2, i%2) - vec3(1)); // -1, 1
        const int indices[6*4] = { 0,2,3,1, 0,1,5,4, 0,4,6,2, 1,3,7,5, 2,6,7,3, 4,5,7,6};
        const bgr3f colors[6] = {bgr3f(0,0,1), bgr3f(0,1,0), bgr3f(1,0,0), bgr3f(1,1,0), bgr3f(1,0,1), bgr3f(0,1,1)};
        for(int i: range(6)) {
            faces[i*2+0] = {{position[indices[i*4+2]], position[indices[i*4+1]], position[indices[i*4+0]]}, {vec3(1,0,0), vec3(1,1,0)}, colors[i]};
            faces[i*2+1] = {{position[indices[i*4+3]], position[indices[i*4+2]], position[indices[i*4+0]]}, {vec3(1,1,0), vec3(0,1,0)}, colors[i]};
            {auto& f = faces[i*2+0]; f.attributes[2] = vec3(f.position[0].z, f.position[1].z, f.position[2].z);}
            {auto& f = faces[i*2+1]; f.attributes[2] = vec3(f.position[0].z, f.position[1].z, f.position[2].z);}
        }
    }

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

    // Zero-length arrays are not permitted in C++
    /*template<Type... Args> void render(Renderer<sizeof...(Args)>& renderer, mat4 M, float clear[sizeof...(Args)], const ImageH& Z, const Args&... targets) {
        return render<sizeof...(Args)>(renderer, M, clear, Z, (const ImageH[sizeof...(Args)]){ unsafeShare(targets)... });
    }
    template<int C> void render(Renderer<C>& renderer, mat4 M, float clear[C], const ImageH& Z, const ImageH targets[C]) {
        uint2 size = (C ? targets[0] : Z).size;*/
    template<Type... Args> void render(Renderer<sizeof...(Args)>& renderer, mat4 M, float clear[/*sizeof...(Args)*/], const ImageH& Z, const Args&... targets_) {
        //const ImageH targets[sizeof...(Args)] { unsafeShare(targets_)... }; // Zero-length arrays are not permitted in C++
        //uint2 size = (sizeof...(Args) ? targets[0] : Z).size;
        uint2 size = sizeof...(Args) ? (uint2[]){targets_.size...}[0] : Z.size;
        renderer.target.setup(int2(size), 1, clear); // Needs to be setup before pass
        renderer.pass.setup(renderer.target, ref<Face>(faces).size); // Clears bins face counter
        mat4 NDC;
        NDC.scale(vec3(vec2(size*4u)/2.f, 1)); // 0, 2 -> subsample size // *4u // MSAA->4x
        NDC.translate(vec3(vec2(1),0.f)); // -1, 1 -> 0, 2
        M = NDC * M;
        for(const Face& face: faces) {
            vec4 a = M*vec4(face.position[0],1), b = M*vec4(face.position[1],1), c = M*vec4(face.position[2],1);
            if(cross((b/b.w-a/a.w).xyz(),(c/c.w-a/a.w).xyz()).z <= 0) continue; // Backward face culling
            renderer.pass.submit(a,b,c, face.attributes, {face.color});
        }
        renderer.pass.render(renderer.target);
        //renderer.target.resolve(Z, (const ImageH[sizeof...(Args)]){ unsafeShare(targets_)... }); // Zero-length arrays are not permitted in C++
        renderer.target.resolve(Z, targets_...);
    }
};

inline double log2(double x) { return __builtin_log2(x); }

template<> vecf<0> Scene::Shader::shade<0>(FaceAttributes, float, float[V]) const { return {}; }
template<> vec16f<0> Scene::Shader::shade<0>(FaceAttributes, v16sf, v16sf[V]) const { return {}; }

template<> vecf<3> Scene::Shader::shade<3>(FaceAttributes face, float, float varying[V]) const {
    const float u = varying[0], v = varying[1];
    static float cellCount (16);
    const float n = floor(cellCount*u)+floor(cellCount*v); // Integer
    const float m = float(1./2)*n; // Half integer
    const float mod = float(2)*(m-floor(m)); // 0 or 1, 2*fract(n/2) = n%2
    return vecf<3>{{mod*float(face.color.b), mod*float(face.color.g), mod*float(face.color.r)}};
}
template<> vec16f<3> Scene::Shader::shade<3>(FaceAttributes face, v16sf, v16sf varying[V]) const {
    const v16sf u = varying[0], v = varying[1];
    static v16sf cellCount (16);
    const v16sf n = floor(cellCount*u)+floor(cellCount*v); // Integer
    const v16sf m = v16sf(1./2)*n; // Half integer
    const v16sf mod = v16sf(2)*(m-floor(m)); // 0 or 1, 2*fract(n/2) = n%2
    return vec16f<3>{{mod*v16sf(face.color.b), mod*v16sf(face.color.g), mod*v16sf(face.color.r)}};
}
