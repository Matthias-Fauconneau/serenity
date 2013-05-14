#pragma once
#include "volume.h"
#include "matrix.h"

struct Primitive {
    enum { Sphere, Cylinder, Cone } type;
    vec3 position; float radius;
    vec3 axis; //cylinder/cone height is |axis|
};

/// Returns a list of random primitives inside the cube [X,Y,Z] separated from a given minimal distance and with a given maximal size
array<Primitive> randomPrimitives(int X, int Y, int Z, int minimumDistance=3, int maximumSize=255);

/// Rasterizes primitives inside a volume (voxels outside any surface are assigned the maximum value, voxels inside any surface are assigned 0)
void rasterize(Volume16& target, const array<Primitive>& primitives);
