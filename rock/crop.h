#pragma once
#include "operation.h"
#include "vector.h"

struct CropVolume { int3 min,max,size,sampleCount,margin; bool cylinder; };
/// Parses volume to crop from user arguments
CropVolume parseCrop(const Dict& args, int3 sourceMargin, int3 sourceMin, int3 sourceMax, int3 extra=0, int3 minimalMargin=0);
/// Parses volume to crop from user arguments (transforms global cooordinates to input coordinates using crop specifications of input volume)
CropVolume parseGlobalCropAndTransformToInput(const Dict& args, int3 sourceMargin, int3 sourceMin, int3 sourceMax);
