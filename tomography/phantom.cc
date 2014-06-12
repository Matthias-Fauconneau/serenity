#include "phantom.h"
#include "time.h" // Random
#include "project.h"
#include "image.h"

Ellipsoid::Ellipsoid(vec3 scale, vec3 angles, vec3 center, float value) :
    forward(mat3().rotateX(angles.x).rotateY(angles.y).rotateZ(angles.z).scale(scale)),
    inverse(forward.inverse()),
    center(center),
    value(value) {}

bool Ellipsoid::contains(vec3 point) const { return sq(inverse*(point-center)) < 1; }

mat3 randomRotation(Random& random) {
    float a = 2*PI*random();
    mat3 R = mat3().rotateZ(a);
    float b = 2*PI*random(), c=random();
    vec3 v (cos(b)*sqrt(c), sin(b)*sqrt(c), sqrt(1-sqrt(c)));
    mat3 H( 2*v.x*v-vec3(1,0,0), 2*v.y*v-vec3(0,1,0), 2*v.z*v-vec3(0,0,1) );
    return H*R;
}

/*// Oriented bounding box intersection (FIXME: ellipsoid intersections)
bool overlaps(const mat4& a, const mat4& b) {
    vec3 min = inf, max = -inf;
    for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) {
        vec3 t = a.inverse()*b*vec3(i?-1:1,j?-1:1,k?-1:1); // Projects B corners to A
        min = ::min(min, t), max = ::max(max, t);
    }
    return min < vec3(1) && max > vec3(-1);
}
bool intersects(const mat4& a, const mat4& b) { return overlaps(a, b) && overlaps(b, a); }*/

Phantom::Phantom(uint count) {
    // Synthesizes random oriented ellipsoids
    const float maximumRadius = 1./4;
    for(Random random; ellipsoids.size < count; ) {
        vec3 radius = maximumRadius*vec3(random(),random(),random());
        if(max(max(radius.x,radius.y),radius.z) > 4*min(min(radius.x,radius.y),radius.z)) continue; // Limits aspect ratio
        const float margin = max(max(radius.x,radius.y),radius.z);
        vec2 rz = (1-margin)*(2.f*vec2(random(),random())-vec2(1)); // Fits cylinder
        const float a = 2*PI*random();
        vec3 center = vec3(rz[0]*cos(a), rz[0]*sin(a), rz[1]);
        mat4 ellipsoid = mat4(randomRotation(random) * mat3().scale(radius)).translate(center);
        //for(const mat4& o: ellipsoids) if(intersects(ellipsoid, o)) goto break_; // Prevents intersections
        /*else*/ ellipsoids << Ellipsoid((mat3)ellipsoid, ((mat3)ellipsoid).inverse(), center, 1);
        //break_:;
    }
}

buffer<float> Phantom::volume(int3 size) const {
    buffer<float> data (size.x*size.y*size.z);
    for(float& v: data) v=0;
    for(Ellipsoid e: ellipsoids) { // Rasterizes ellipsoids
        // Computes world axis-aligned bounding box of object's oriented bounding box
        vec3 O = e.center, min = O, max = O; // Initialize min/max to origin
        for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) { // Bounds each corner
            vec3 corner = O + e.forward*vec3(i?-1:1,j?-1:1,k?-1:1);
            min=::min(min, corner), max=::max(max, corner);
        }
        min = ::max(vec3(size-int3(1))*(min+vec3(1))/2.f, vec3(0)), max = ::min(ceil(vec3(size-int3(1))*(max+vec3(1))/2.f), vec3(size));

        const vec3 origin = e.inverse * (vec3(-1) - e.center);
        const vec3 vx = e.inverse[0] / vec3(float(size.x-1)/2);
        const vec3 vy = e.inverse[1] / vec3(float(size.y-1)/2);
        const vec3 vz = e.inverse[2] / vec3(float(size.z-1)/2);

        float* volumeData = (float*)data.data;

        for(int z: range(min.z, max.z)) {
            const vec3 pz = origin + float(z) * vz;
            float* volumeZ = volumeData + z * size.y * size.x;
            for(int y: range(min.y, max.y)) {
                const vec3 pzy = pz + float(y) * vy;
                float* volumeZY = volumeZ + y * size.x;
                for(int x: range(min.x, max.x)) {
                    const vec3 pzyx = pzy + float(x) * vx;
                    if(sq(pzyx) < 1) volumeZY[x] += e.value;
                }
            }
        }
    }
    for(float v: data) assert_(v>=0);
    return data;
}

void Phantom::project(const ImageF& target, int3 volumeSize, const Projection& projection) const {
    vec3 scale = 1.f/(vec3(volumeSize-int3(1))/2.f);
    target.data.clear();
    for(Ellipsoid e: ellipsoids) {
        // Computes projection axis-aligned bounding box of object's oriented bounding box
        /*vec2 O = projection.project(e.center), min = O, max = O; // Initialize min/max to origin
        for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) { // Bounds each corner
            vec2 corner = O + projection.project(e.forward * vec3(i?-1:1,j?-1:1,k?-1:1));
            min=::min(min, corner), max=::max(max, corner);
        }
        min = ::max(min+vec2(size-int2(1))/2.f, vec2(0)), max = ::min(ceil(max+vec2(size-int2(1))/2.f), vec2(size));*/
        int2 min = 0, max = target.size;

        const vec3 O = e.inverse * (scale * projection.imageToWorld[3].xyz() - e.center);
        const float c = dot(O, O) - 1;
        for(int y: range(min.y, max.y)) {
            for(int x: range(min.x, max.x)) {
                const vec3 D = e.inverse * (scale * (float(x) * projection.imageToWorld[0].xyz() + float(y) * projection.imageToWorld[1].xyz() + projection.imageToWorld[2].xyz()));
                const float a = dot(D, D);
                const float b = dot(D, O);
                float d = b*b - a*c;
                if(d<=0) continue;
                float t1 = - b - sqrt(d);
                float t2 = - b + sqrt(d);
                float l = (t2 - t1) / a;
                target(x,y) += l * e.value;
            }
        }
    }
}
