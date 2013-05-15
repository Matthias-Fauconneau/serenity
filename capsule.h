#pragma once
#include "volume.h"

struct Capsule { vec3 a, b; float radius; };
template<> inline string str(const Capsule& o) { return str(o.a,o.b,o.radius); }

/// Returns a list of random capsules inside the cube [X,Y,Z] separated from a given minimal distance and with a given maximal length and radius
array<Capsule> randomCapsules(float X, float Y, float Z, float minimumDistance=3, float maximumLength=255, float maximumRadius=255);

/// Rasterizes capsules inside a volume (voxels outside any surface are assigned the maximum value, voxels inside any surface are assigned zero)
void rasterize(Volume16& target, const array<Capsule>& capsules);
