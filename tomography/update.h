#pragma once
#include "reconstruction.h"

/// Backprojects value onto voxels along ray
static inline void backproject(float4 position, float4 step, float4 end, const CylinderVolume& volume, const float* data, v8sf value) {
    for(;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
        const v4si integerPosition = cvttps2dq(position); // Converts position to integer coordinates
        const v4si index = dot4(integerPosition, volume.stride); // to voxel index
        const v4si indices = index + volume.offset; // to 4 voxel indices
        const v8sf samples = gather2(data, indices); // Gather samples
        const v8sf fract = abs(dup(position - cvtdq2ps(integerPosition)) - _00001111f); // Computes trilinear interpolation coefficients
        const v8sf weights = shuffle8(fract, fract, 4,4,4,4, 0,0,0,0) * shuffle8(fract, fract, 5,5, 1,1, 5,5, 1,1) * shuffle8(fract, fract, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
        const v8sf result = samples + weights * value;
        (v2sf&)(data[indices[0]]) = __builtin_shufflevector(result, result, 0, 1);
        (v2sf&)(data[indices[1]]) = __builtin_shufflevector(result, result, 2, 3);
        (v2sf&)(data[indices[2]]) = __builtin_shufflevector(result, result, 4, 5);
        (v2sf&)(data[indices[3]]) = __builtin_shufflevector(result, result, 6, 7);
        position += step;
        if(mask(position > end)) return;
    }
}

// Computes regularization: QtQ = lambda · ( I + alpha · CtC )
static inline float regularization(const VolumeF& volume, const uint x, const uint y, const uint z, const uint i) {
    const uint X = volume.sampleCount.y, Y = volume.sampleCount.y, XY=X*Y, Z = volume.sampleCount.z;
    float* data = volume.data + i;
    float Ix = data[0];
    float CtCx = 0;
    for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) {
        if(uint(x+dx) < X && uint(y+dy) < Y && uint(z+dz) < Z) {
            float Cx = Ix - data[dz*int(XY) + dy*int(X) + dx];
            CtCx += sq(Cx);
        }
    }
    float QtQx = 0 * Ix + 1 * CtCx;
    return QtQx;
}
