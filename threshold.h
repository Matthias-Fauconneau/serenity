#pragma once
#include "volume.h"

float computeDensityThreshold(const Volume16& volume);
void threshold(Volume32& target, const Volume16& source, float threshold);
