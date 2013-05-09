#pragma once
#include "volume.h"

/// Rasterizes each distance field voxel as a ball (with maximum blending)
void rasterize(Volume16& target, const Volume16& source);

