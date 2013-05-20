#pragma once
#include "volume.h"

/// Computes integer medial axis
void integerMedialAxis(Volume16& target, const Volume16& positionX, const Volume16& positionY, const Volume16& positionZ, int minimalSqRadius=3);

