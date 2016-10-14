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
#define CHECK 1
#if CHECK
        static constexpr int V = 3;
#else
        static constexpr int V = 0;
#endif
        static constexpr bool blend = false; // Disables unnecessary blending

#if CHECK
        bgra4v16sf operator()(FaceAttributes face, v16sf unused varying[V]) const {
            bgra4v16sf Y;
            for(int i: range(16)) {
                float x[V];
                for(int j: range(V)) x[j] = varying[j][i];
                bgra4f y = operator()(face, x);
                Y.b[i] = y.b;
                Y.g[i] = y.g;
                Y.r[i] = y.r;
                Y.a[i] = y.a;
            }
            return Y;
        }
        bgra4f operator()(FaceAttributes, float unused varying[V]) const {
            return bgra4f(vec3((1+varying[2])/2.f), 1.f); // Z
            const float u = varying[0], v = varying[1];
            const bool white = (int(16*u)+int(16*v))%2;
            return white ? bgra4f(1,1,1, 1.f) : bgra4f(0,0,0, 1.f);
        }
#else
        bgra4v16sf operator()(FaceAttributes face, v16sf unused varying[V]) const {
            return {v16sf(face.color.b), v16sf(face.color.g), v16sf(face.color.r), _1f};
        }
        bgra4f operator()(FaceAttributes face, float unused varying[V]) const {
            return bgra4f(face.color, 1.f);
        }
#endif
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

    struct Renderer {
        RenderPass<Scene::Shader> pass; // Face bins
        RenderTarget target; // Sample tiles
        Renderer(const Scene& scene) : pass(scene.shader) {}
    };

    void render(Renderer& renderer, const ImageH& Z, const ImageH& B, const ImageH& G, const ImageH& R, mat4 M) {
        renderer.target.setup(int2(Z.size)); // Needs to be setup before pass
        renderer.pass.setup(renderer.target, ref<Face>(faces).size); // Clears bins face counter
        mat4 NDC;
        NDC.scale(vec3(vec2(Z.size*4u)/2.f, 1)); // 0, 2 -> subsample size // *4u // MSAA->4x
        NDC.translate(vec3(vec2(1),0.f)); // -1, 1 -> 0, 2
        M = NDC * M;
        for(const Face& face: faces) {
            vec4 A = M*vec4(face.position[0],1), B = M*vec4(face.position[1],1), C = M*vec4(face.position[2],1);
            if(cross((B/B.w-A/A.w).xyz(),(C/C.w-A/A.w).xyz()).z <= 0) continue; // Backward face culling
            renderer.pass.submit(A,B,C, face.attributes, {face.color});
        }
        renderer.pass.render(renderer.target);
        renderer.target.resolve(Z, B, G, R);
    }
    void render(Renderer& renderer, const ImageH& Z, mat4 M) {
        /*renderer.target.setup(int2(Z.size)); // Needs to be setup before pass
        renderer.pass.setup(renderer.target, ref<Face>(faces).size); // Clears bins face counter
        mat4 NDC;
        NDC.scale(vec3(vec2(Z.size*4u)/2.f, 1)); // 0, 2 -> subsample size // *4u // MSAA->4x
        NDC.translate(vec3(vec2(1),0.f)); // -1, 1 -> 0, 2
        M = NDC * M;
        for(const Face& face: faces) {
            vec3 A = M*face.position[0], B = M*face.position[1], C = M*face.position[2];
            if(cross(B-A,C-A).z <= 0) continue; // Backward face culling
            vec3 attributes[0];
            renderer.pass.submit(A,B,C, attributes, {face.color});
        }
        renderer.pass.render(renderer.target);
        renderer.target.resolve(Z);*/
        ImageH dummy(Z.size); return render(renderer, Z, dummy, dummy, dummy, M);
    }
};
