#pragma once
#include "volume.h"

/// Computes one pass of perpendicular bisector distance field algorithm
template<bool last> void PerpendicularBisectorEuclideanDistanceTransform(Volume32& target, const Volume32& source, uint X, uint Y, uint Z);
