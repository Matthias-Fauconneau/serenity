#include "phantom.h"
#include "time.h" // Random

Ellipsoid::Ellipsoid(vec3 scale, vec3 angles, vec3 center, float value) :
    mat3(mat3().rotateX(angles.x).rotateY(angles.y).rotateZ(angles.z).scale(scale).inverse()),
    center(center),
    value(value) {}

bool Ellipsoid::contains(vec3 point) const { return sq(*this*(point-center)) < 1; }

float Ellipsoid::intersect(vec3 point, vec3 direction) const {
    vec3 p0 = *this * direction;
    vec3 p1 = *this * (point-center);
    float a = sq(p0);
    float b = dot(p0, p1);
    float c = sq(p1) - 1;
    float d = sq(b) - a*c;
    if(d<=0) return 0;
    float t1 = - b - sqrt(d);
    float t2 = - b + sqrt(d);
    return (t2 - t1) / a;
}

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
            //{ a   ,  b  ,  c  }, { phi, theta, psi}, {    x,     y,    z}, A
            {{.6900, .920, .900}, {     0   , 0, 0}, {    0,     0,    0},   1},
            {{.6624, .874, .880}, {     0   , 0, 0}, {    0,     0,    0},-0.8},
            {{.4100, .160, .210}, {radf(108), 0, 0}, { -.22,     0, -.25},-0.2},
            {{.3100, .110, .220}, {radf(72) , 0, 0}, {  .22,     0, -.25},-0.2},
            {{.2100, .250, .500}, {     0   , 0, 0}, {    0,   .35, -.25}, 0.2},
            {{.0460, .046, .046}, {     0   , 0, 0}, {    0,    .1, -.25}, 0.2},
            {{.0460, .023, .020}, {     0   , 0, 0}, { -.08,  -.65, -.25}, 0.1},
            {{.0460, .023, .020}, {radf(90) , 0, 0}, {  .06,  -.65, -.25}, 0.1},
            {{.0560, .040, .100}, {radf(90) , 0, 0}, {  .06, -.105, .625}, 0.2},
            {{.0560, .056, .100}, {     0   , 0, 0}, {    0,  .100, .625},-0.2}
        };
    } else { /// Synthesizes random oriented ellipsoids
        const float maximumRadius = 1./4;
        for(Random random; ellipsoids.size < count; ) {
            vec3 radius = maximumRadius*vec3(random(),random(),random());
            if(max(max(radius.x,radius.y),radius.z) > 4*min(min(radius.x,radius.y),radius.z)) continue; // Limits aspect ratio
            const float margin = max(max(radius.x,radius.y),radius.z);
            vec3 center = (1-margin)*(2.f*vec3(random(),random(),random())-vec3(1));
            mat4 ellipsoid = mat4(randomRotation(random) * mat3().scale(radius)).translate(center);
            //for(const mat4& o: ellipsoids) if(intersects(ellipsoid, o)) goto break_; // Prevents intersections
            /*else*/ ellipsoids << Ellipsoid((mat3)ellipsoid.inverse(), center, 1);
            //break_:;
        }
    }
}

VolumeF Phantom::volume(int3 size) const {
    VolumeF target (size);
    for(int z: range(size.z)) for(int y: range(size.y)) for(int x: range(size.x)) {
        const vec3 point = vec3(x,y,z)*2.f/vec3(size)-vec3(1);
        for(Ellipsoid e: ellipsoids) if(e.contains(point)) target(x,y,z) += e.value;
    }
    return target;
}

ImageF Phantom::project(int2 size, mat4 projection) const {
    ImageF target (size);
    target.data.clear();
    for(uint y: range(target.height)) for(uint x: range(target.width)) {
        vec3 origin    = projection.inverse() * (vec3(x,y,0) * (2.f/min(size.x,size.y)) - vec3(1,1,0));
        vec3 direction = normalize(projection.transpose() * vec3(0,0,1));
        for(Ellipsoid e: ellipsoids) target(x,y) += e.intersect(origin, direction) * e.value;
    }
    return target;
}
