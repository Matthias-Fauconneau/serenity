#include "scene.h"

inline Scene box(const float size = 1, const bool reverse = false) {
    vec3 position[8];
    for(int i: range(8)) position[i] = size * (2.f * vec3(i/4, (i/2)%2, i%2) - vec3(1)); // -1, 1
    const int indices[6*4] = {0,2,3,1, 0,1,5,4, 0,4,6,2, 1,3,7,5, 2,6,7,3, 4,5,7,6};
    const bgr3f colors[6] = {bgr3f(0,0,1), bgr3f(0,1,0), bgr3f(1,0,0), bgr3f(1,1,0), bgr3f(1,0,1), bgr3f(0,1,1)};
    buffer<Scene::Face> faces (6*2);
    for(int i: range(6)) {
        faces[i*2+0] = {{position[indices[i*4+2]], position[indices[i*4+1]], position[indices[i*4+0]]}, {vec3(1,0,0), vec3(1,1,0)}, colors[i]};
        faces[i*2+1] = {{position[indices[i*4+3]], position[indices[i*4+2]], position[indices[i*4+0]]}, {vec3(1,1,0), vec3(0,1,0)}, colors[i]};
    }
    if(reverse) for(Scene::Face& face: faces) {
        swap(face.position[0], face.position[2]);
        for(vec3& attribute: face.attributes) swap(attribute[0], attribute[2]);
    }
    return {vec3(0,0,2*size), ::move(faces)};
}
