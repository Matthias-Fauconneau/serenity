#pragma once
#include "operation.h"
#include "vector.h"

struct Cylinder { int3 min,max; };
struct CropVolume {
    int3 min,max; // Crop bounds (coordinates includes margins, first valid voxel at margin)
    int3 size; // Valid crop volume size (max-min)
    int3 sampleCount; // Sample count allocated in each dimensions (has to be 2ⁿ for tiling to work correctly)
    int3 margin; // Margins from the start of the allocated volume coordinates to the valid region (sampleCount-size)/2
};
Cylinder parseCylinder(string cylinder, int3 center, int3 extra=0);
/// Aligns volume to crop from cylinder
CropVolume alignCrop(int3 min, int3 max, int3 sourceMargin, int3 sourceMin, int3 sourceMax, int3 minimalMargin=0);
/// Parses volume to crop from user arguments
CropVolume parseCrop(const Dict& args, int3 sourceMargin, int3 sourceMin, int3 sourceMax, int3 extra=0, int3 minimalMargin=0);
/// Parses volume to crop from user arguments (transforms global cooordinates to input coordinates using crop specifications of input volume)
CropVolume parseGlobalCropAndTransformToInput(const Dict& args, int3 sourceMargin, int3 sourceMin, int3 sourceMax);
