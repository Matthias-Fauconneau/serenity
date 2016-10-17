#include "light.h"
#include "scene.h"

struct Analyze {
    Analyze() {
        float sum = 0;
        for(const Scene::Face& face: Scene().faces) {
            const vec3 a = face.position[0], b = face.position[1], c = face.position[2];
            const vec3 N = cross(b-a,c-a); // Face normal
            vec2 v = clamp(vec2(-2), vec2(N.x?N.x/N.z:0, N.y?N.y/N.z:0), vec2(2)); // Closest view vector (normalized by Z)
            const float PSA = dot(normalize(vec3(v,1)), N) / 2; // Projected triangle surface area
            log(N, v, PSA);
            if(PSA<0) continue; // Backward face culling
            sum += PSA;
        }
        log(sum);
    }
} app;
