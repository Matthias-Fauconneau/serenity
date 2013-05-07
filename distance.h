#pragma once
#include "volume.h"

/// Computes one pass of perpendicular bisector distance field algorithm
template<bool last> void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, /*Volume16& position,*/ const Volume32& source, uint X, uint Y, uint Z/*, uint xStride, uint yStride, uint zStride*/);

