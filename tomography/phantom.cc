#include "phantom.h"
#include "time.h" // Random

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

// Oriented bounding box intersection (FIXME: ellipsoid intersections)
bool overlaps(const mat4& a, const mat4& b) {
    vec3 min = inf, max = -inf;
    for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) {
        vec3 t = a.inverse()*b*vec3(i?-1:1,j?-1:1,k?-1:1); // Projects B corners to A
        min = ::min(min, t), max = ::max(max, t);
    }
    return min < vec3(1) && max > vec3(-1);
}
bool intersects(const mat4& a, const mat4& b) { return overlaps(a, b) && overlaps(b, a); }

Phantom::Phantom(uint count) {
    if(!count) { /// Sheep-Logan head phantom
        ellipsoids = {
            // Yu H, Ye Y, Wang G, Katsevich-Type Algorithms for Variable Radius Spiral Cone-Beam CT
            //   { a   ,  b  ,  c  }, { phi, theta,psi}, {    x,     y,    z}, A
            {vec3(.6900, .920, .900), {     0   , 0, 0}, {    0,     0,    0}, 1  },
            {vec3{.6624, .874, .880}, {     0   , 0, 0}, {    0,     0,    0},-0.8},
            {vec3{.4100, .160, .210}, {radf(108), 0, 0}, { -.22,     0, -.25},-0.2},
            {vec3{.3100, .110, .220}, {radf(72) , 0, 0}, {  .22,     0, -.25},-0.2},
            {vec3{.2100, .250, .500}, {     0   , 0, 0}, {    0,   .35, -.25}, 0.2},
            {vec3{.0460, .046, .046}, {     0   , 0, 0}, {    0,    .1, -.25}, 0.2},
            {vec3{.0460, .023, .020}, {     0   , 0, 0}, { -.08,  -.65, -.25}, 0.1},
            {vec3{.0460, .023, .020}, {radf(90) , 0, 0}, {  .06,  -.65, -.25}, 0.1},
            {vec3{.0560, .040, .100}, {radf(90) , 0, 0}, {  .06, -.105, .625}, 0.2},
            {vec3{.0560, .056, .100}, {     0   , 0, 0}, {    0,  .100, .625},-0.2}
        };
    } else { /// Synthesizes random oriented ellipsoids
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
}

void Phantom::volume(const VolumeF& volume) const {
    int3 size = volume.sampleCount;
    for(Ellipsoid e: ellipsoids) { // Rasterizes ellipsoids
        // Computes world axis-aligned bounding box of object's oriented bounding box
        vec3 O = e.center, min = O, max = O; // Initialize min/max to origin
        for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) { // Bounds each corner
            vec3 corner = O + e.forward*vec3(i?-1:1,j?-1:1,k?-1:1);
            min=::min(min, corner), max=::max(max, corner);
        }
        min = ::max(vec3(size-int3(1))*(min+vec3(1))/2.f, vec3(0)), max = ::min(ceil(vec3(size-int3(1))*(max+vec3(1))/2.f), vec3(size));

        const float4 origin = e.inverse * (vec3(-1) - e.center);
        const float4 vx = e.inverse[0] / vec3(float(size.x-1)/2);
        const float4 vy = e.inverse[1] / vec3(float(size.y-1)/2);
        const float4 vz = e.inverse[2] / vec3(float(size.z-1)/2);

        float* volumeData = (float*)volume.data.data;

        for(int z: range(min.z, max.z)) {
            const float4 pz = origin + float4(z) * vz;
            float* volumeZ = volumeData + z * volume.sampleCount.y * volume.sampleCount.x;
            for(int y: range(min.y, max.y)) {
                const float4 pzy = pz + float4(y) * vy;
                float* volumeZY = volumeZ + y * volume.sampleCount.x;
                for(int x: range(min.x, max.x)) {
                    const float4 pzyx = pzy + float4(x) * vx;
                    if(mask(dot4(pzyx, pzyx) < _1f)) volumeZY[x] += e.value;
                }
            }
        }
    }
    for(float v: volume.data) assert_(v>=0);
}

void Phantom::project(const ImageF& target, const Projection& projection) const {
    assert_(target.size() == projection.imageSize);
    //target.data.clear();
    for(Ellipsoid e: ellipsoids) {
        // Computes projection axis-aligned bounding box of object's oriented bounding box
        /*vec2 O = projection.project(e.center), min = O, max = O; // Initialize min/max to origin
        for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) { // Bounds each corner
            vec2 corner = O + projection.project(e.forward * vec3(i?-1:1,j?-1:1,k?-1:1));
            min=::min(min, corner), max=::max(max, corner);
        }
        min = ::max(min+vec2(size-int2(1))/2.f, vec2(0)), max = ::min(ceil(max+vec2(size-int2(1))/2.f), vec2(size));*/
        int2 min = 0, max = target.size();

        const float4 O = e.inverse * (toVec3(projection.origin)/(vec3(projection.volumeSize-int3(1))/2.f) - e.center);
        for(int y: range(min.y, max.y)) {
            for(int x: range(min.x, max.x)) {
                const float4 D = e.inverse * (projection.pixelRay(x, y)/(vec3(projection.volumeSize-int3(1))/2.f));
                const float a = dot4(D, D)[0];
                const float b = dot4(D, O)[0];
                const float c = dot4(O, O)[0] - 1;
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
