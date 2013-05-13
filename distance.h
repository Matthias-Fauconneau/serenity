#pragma once
#include "volume.h"

/// Computes one pass of perpendicular bisector distance field algorithm
void perpendicularBisectorEuclideanDistanceTransform(Volume32& distanceX, Volume16& positionX, const Volume32& source, int X, int Y, int Z);
void perpendicularBisectorEuclideanDistanceTransform(Volume32& distanceXY, Volume16& positionX, Volume16& positionY, const Volume32& source, const Volume16& sourceX, int X, int Y, int Z);
void perpendicularBisectorEuclideanDistanceTransform(Volume32& distanceXYZ, Volume16& positionX, Volume16& positionY, Volume16& positionZ, const Volume32& source, const Volume16& sourceX, const Volume16& sourceY, int X, int Y, int Z);
