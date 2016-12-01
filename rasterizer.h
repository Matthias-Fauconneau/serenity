#pragma once
#include "raster.h"

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
    const Scene& scene;
    TextureShader(const Scene& scene) : scene(scene) {}

    inline Vec<v16sf, C> shade(const uint id, FaceAttributes face, v16sf z, v16sf varying[V], v16si mask) const { return Shader::shade(id, face, z, varying, mask); }
    inline Vec<float, 3> shade(const uint, FaceAttributes face, float, float varying[V]) const {
        bgr3f bgr = sample(scene, face, varying[0],  varying[1]);
        return {{bgr.b, bgr.g, bgr.r}};
    }
};

template<Type Shader> struct Rasterizer {
    Shader shader; // Instance holds any uniforms (currently none)
    RenderPass<Shader> pass; // Face bins
    RenderTarget<Shader::C> target; // Sample tiles
    Rasterizer(Shader&& shader_={}) : shader(::move(shader_)), pass(shader) {}
};

template<Type Shader, Type... Args>
void rasterize(Rasterizer<Shader>& renderer, const Scene& scene, mat4 M, float clear[/*sizeof...(Args)*/], const ImageH& depth, const Args&... targets_) {
    const ImageH targets[sizeof...(Args)] { unsafeShare(targets_)... }; // Zero-length arrays are not permitted in C++
    uint2 size = (sizeof...(Args) ? targets[0] : depth).size;
    renderer.target.setup(int2(size), 1, clear); // Needs to be setup before pass
    renderer.pass.setup(renderer.target, scene.size); // Clears bins face counter
    mat4 NDC;
    NDC.scale(vec3(vec2(size*4u)/2.f, 1)); // 0, 2 -> subsample size // *4u // MSAA->4x
    NDC.translate(vec3(vec2(1), 0.f)); // -1, 1 -> 0, 2
    M = NDC * M;
    for(size_t face : range(scene.size)) {
        const vec3 A (scene.X0[face], scene.Y0[face], scene.Z0[face]);
        const vec3 B (scene.X1[face], scene.Y1[face], scene.Z1[face]);
        const vec3 C (scene.X2[face], scene.Y2[face], scene.Z2[face]);

        const vec4 a = M*vec4(A,1), b = M*vec4(B,1), c = M*vec4(C,1);
        if(cross((b/b.w-a/a.w).xyz(),(c/c.w-a/a.w).xyz()).z >= 0) continue; // Backward face culling

        renderer.pass.submit(a,b,c, (vec3[]){vec3(scene.U0[face],scene.U1[face],scene.U2[face]),
                                             vec3(scene.V0[face],scene.V1[face],scene.V2[face]),
                                             vec3(A.x,B.x,C.x),
                                             vec3(A.y,B.y,C.y),
                                             vec3(A.z,B.z,C.z),
                                             vec3(scene.TX0[face],scene.TX1[face],scene.TX2[face]),
                                             vec3(scene.TY0[face],scene.TY1[face],scene.TY2[face]),
                                             vec3(scene.TZ0[face],scene.TZ1[face],scene.TZ2[face]),
                                             vec3(scene.BX0[face],scene.BX1[face],scene.BX2[face]),
                                             vec3(scene.BY0[face],scene.BY1[face],scene.BY2[face]),
                                             vec3(scene.BZ0[face],scene.BZ1[face],scene.BZ2[face]),
                                             vec3(scene.NX0[face],scene.NX1[face],scene.NX2[face]),
                                             vec3(scene.NY0[face],scene.NY1[face],scene.NY2[face]),
                                             vec3(scene.NZ0[face],scene.NZ1[face],scene.NZ2[face])}, face);
    }
    renderer.pass.render(renderer.target);
    renderer.target.resolve(depth, targets);
}
