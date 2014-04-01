#include "phantom.h"

Ellipsoid::Ellipsoid(float value, vec3 scale, vec3 center, vec3 angles) :
    M(mat3().scale(scale).rotateX(angles.x).rotateY(angles.y).rotateZ(angles.z).inverse()),
    center(center),
    value(value) {}

bool Ellipsoid::contains(vec3 point) const { return sq(M*(point-center)) < 1; }

float Ellipsoid::intersect(vec3 point, vec3 direction) const {
    vec3 p0 = M * direction;
    vec3 p1 = M * (point-center);
    float a = sq(p0);
    float b = dot(p0, p1);
    float c = sq(p1) - 1;
    float d = sq(b) - a*c;
    if(d<=0) return 0;
    float t1 = - b - sqrt(d);
    float t2 = - b + sqrt(d);
    return (t2 - t1) / a;
}

Phantom::Phantom() : ellipsoids(copy(buffer<Ellipsoid>({
// Yu H, Ye Y, Wang G, Katsevich-Type Algorithms for Variable Radius Spiral Cone-Beam CT
//A,   { a   ,  b  ,  c  }, {    x,     y,    z}, { phi, theta, psi}
{ 1,   {.6900, .920, .900}, {    0,     0,    0}, {   0, 0, 0}},
{-0.8, {.6624, .874, .880}, {    0,     0,    0}, {   0, 0, 0}},
{-0.2, {.4100, .160, .210}, { -.22,     0, -.25}, { 108, 0, 0}},
{-0.2, {.3100, .110, .220}, {  .22,     0, -.25}, {  72, 0, 0}},
{ 0.2, {.2100, .250, .500}, {    0,   .35, -.25}, {   0, 0, 0}},
{ 0.2, {.0460, .046, .046}, {    0,    .1, -.25}, {   0, 0, 0}},
{ 0.1, {.0460, .023, .020}, { -.08,  -.65, -.25}, {   0, 0, 0}},
{ 0.1, {.0460, .023, .020}, {  .06,  -.65, -.25}, {  90, 0, 0}},
{ 0.2, {.0560, .040, .100}, {  .06, -.105, .625}, {  90, 0, 0}},
{-0.2, {.0560, .056, .100}, {    0,  .100, .625}, {   0, 0, 0}}
#endif
}))) {}

VolumeF Phantom::volume(int3 size) const {
    VolumeF target (size);
    for(int z: range(size.z)) for(int y: range(size.y)) for(int x: range(size.x)) {
        for(Ellipsoid e: ellipsoids) if(e.contains(vec3(x,y,z)*2.f/vec3(size)-vec3(1))) target(x,y,z) += e.value;
    }
    return target;
}

ImageF Phantom::project(int2 size, mat4 projection) const {
    ImageF target (size);
    target.data.clear();
    for(uint y: range(target.height)) for(uint x: range(target.width)) {
        vec3 origin    = projection.inverse() * (vec3(x,y,0) * (2.f/min(size.x,size.y)) - vec3(1,1,0));
        constvec3 direction = normalize(projection.transpose() * vec3(0,0,1));
        for(Ellipsoid e: ellipsoids) target(x,y) += e.intersect(origin, direction) * e.value;
    }
    return target;
}
