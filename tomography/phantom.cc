#include "phantom.h"

Ellipsoid::Ellipsoid(float value, vec3 scale, vec3 origin, vec3 angles) :
    mat4(mat4().scale(scale).rotateX(angles.x).rotateY(angles.y).rotateZ(angles.z).translate(origin)),
    //mat4(mat4().translate(origin).rotateX(angles.x).rotateY(angles.y).rotateZ(angles.z).scale(scale)),
    value(value) {}

Ellipsoid Ellipsoid::inverse(const mat4& m) const { return { (m * (mat4)*this).inverse(), value}; }

Phantom::Phantom() : ellipsoids(copy(buffer<Ellipsoid>({
//A,   { a   ,  b  ,  c  }, {    x,     y,    z}, { phi, theta, psi}
#if 0
{ 1,   {0.5    ,0.5    ,0.5    }, {    0,     0,    0}, {   0, 0, 0}}
#else
// Yu H, Ye Y, Wang G, Katsevich-Type Algorithms for Variable Radius Spiral Cone-Beam CT
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
    buffer<Ellipsoid> inverseEllipsoids (ellipsoids.size);
    mat4 projection = mat4().scale(vec3(size)/2.f).translate(1);
    for(uint i: range(ellipsoids.size)) inverseEllipsoids[i] =  ellipsoids[i].inverse(projection);

    VolumeF target (size);
    for(int z: range(size.z)) for(int y: range(size.y)) for(int x: range(size.x)) {
        for(Ellipsoid e: inverseEllipsoids) if(e.contains(vec3(x,y,z))) target(x,y,z) += e.value;
    }
    return target;
}

ImageF Phantom::project(int2 size, mat4 projection) const {
    buffer<Ellipsoid> inverseEllipsoids (ellipsoids.size);
    projection = mat4().scale(vec3(vec2(min(size.x,size.y)/2),0)).translate(vec3(1,1,0)) * projection;
    for(uint i: range(ellipsoids.size)) {
        inverseEllipsoids[i] = ellipsoids[i].inverse(projection);
    }

    ImageF target (size);
    target.data.clear();
    for(uint y: range(target.height)) for(uint x: range(target.width)) {
        for(Ellipsoid e: inverseEllipsoids) if(e.contains(vec2(x,y))) target(x,y) += e.value;
    }
    return target;
}
