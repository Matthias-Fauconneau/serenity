#pragma once
#include "operation.h"
#include "vector.h"

struct CropVolume {
    int3 min,max; // Crop bounds (coordinates includes margins, first valid voxel at margin)
    int3 size; // Valid crop volume size (max-min)
    int3 sampleCount; // Sample count allocated in each dimensions (has to be 2ⁿ for tiling to work correctly)
    int3 margin; // Margins from the start of the allocated volume coordinates to the valid region (sampleCount-size)/2
    int3 origin; // Crop origin
    bool cylinder;
};
/// Parses volume to crop from user arguments
CropVolume parseCrop(const Dict& args, int3 min, int3 max, int3 origin, int3 extra=0);
