#pragma once
#include "volume.h"

/// Segments by setting values over a fixed threshold to ∞ (2³²-1) and to x² otherwise (for distance X input)
void threshold(Volume32& target, const Volume16& source, float threshold);
