#pragma once
#include "projection.h"
#include "volume.h"
#include "time.h"

struct PorousRock {
    const float airDensity = 0.001; // Houndsfield ?
    const float containerDensity = 5.6; // Pure iron
    int3 size;
    const float maximumRadius = 8; // vx
    const float rate = 1./(size.z==1?sq(maximumRadius):cb(maximumRadius)); // 1/vx
    struct GrainType { const float probability; /*relative concentration*/ const float density; buffer<vec4> grains; } types[3] = {/*Rutile*/{0.7, 4.20,{}}, /*Siderite*/{0.2, 3.96,{}}, /*NaMontmorillonite*/{0.1, 2.65,{}}};
    const vec3 volumeCenter = vec3(size-int3(1))/2.f;
    const float volumeRadius = volumeCenter.x;
    const float innerRadius = (1-4./100) * volumeRadius;
    const float outerRadius = (1-2./100) * volumeRadius;
    const uint grainCount = rate*size.z*size.y*size.x;
    float factor = 0;
    vec4 largestGrain = 0;

    PorousRock(int3 size, const float maximumRadius);
    VolumeF volume();
    float project(const ImageF& target, const Projection& A, uint index) const;
};

inline VolumeF project(const PorousRock& rock, const Projection& A) {
    VolumeF volume(A.projectionSize, "b"_);
    Time time;
    for(uint index: range(volume.size.z)) { rock.project(::slice(volume, index), A, index); }
    log("Poisson•A", time);
    return volume;
}
