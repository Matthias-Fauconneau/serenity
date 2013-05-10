#pragma once
#include "volume.h"

struct Ball { int3 position; uint radius; };
/// Rasterizes spheres randomly inside volume
array<Ball> randomBalls(Volume16& target, int minimalDistance=3, int maximumRadius=255);
