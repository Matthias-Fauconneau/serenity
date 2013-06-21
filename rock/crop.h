#pragma once
#include "operation.h"
#include "vector.h"

struct CropVolume { int3 min,max,size,sampleCount,margin; bool cylinder; };
/// Parses volume to crop from user arguments
CropVolume parseCrop(const Dict& args, int3 sourceMin, int3 sourceMax, string box=""_, int3 minimalMargin=0);
