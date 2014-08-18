#pragma once
#include "projection.h"
#include "volume.h"
#include "time.h"

/// Ray-grain intersection
struct Intersection {
    float t; /// Intersection distance along the ray
    int index; /// 1+index of the grain type entered. Negated for an exit intersection.
};
inline bool operator <(const Intersection& a, const Intersection& b) { return a.t < b.t; }

/// Synthetic sample model of a metallic cylinder filled with mineral grains
struct PorousRock {
    // Sample size
    int3 size; // [vx]
    const double sampleSize = 6e-3; // [m]
    const double mToVx = size.x / sampleSize; // [vx/m]
    // Source energy
    const double photonEnergy = 40.0; // [keV]
    const double electronEnergy = 511; // Electron rest mass mₑ [keV]
    const double alpha = photonEnergy/electronEnergy; // hν/mc²
    const double S = (1+alpha)/sq(alpha)*(2*(1+alpha)/(1+2*alpha) - ln(1+2*alpha)/alpha) + ln(1+2*alpha)/(2*alpha) - (1+3*alpha)/sq(1+2*alpha); // Scattering coefficient (Klein-Nishina 1928)
    const double K2 = 0.0097; // Experiment dependent empirical constant [m²/kg]
    const double B = K2*S; // Attenuation coefficient from Compton scattering (Alvarez 1976) [m²?] (Z/A?)
    // Materials
    const double Bvx = B / mToVx ; // Attenuation coefficient (1/vx) from density (kg/m³) [m³/kg·1/vx]
    const double airDensity = 0.001e3; // [kg/m³]
    const double airAttenuation = Bvx * airDensity; // [1/vx]
    const double containerDensity = 2.70e3; // Pure aluminium [kg/m³]
    const float containerAttenuation = Bvx * containerDensity;
    const double minimumGrainRadiusM = 100e-6; // [m]
    const float minimumGrainRadiusVx = minimumGrainRadiusM * mToVx; // [vx]
    const double maximumGrainRadiusM = 250e-6; // [m]
    const float maximumGrainRadiusVx = maximumGrainRadiusM * mToVx; // [vx]
    const float rate = 1./cb(maximumGrainRadiusVx); // Average number of grains per voxels [1/vx]
    struct GrainType { const float relativeGrainCount; const float attenuation; /*[1/vx]*/ buffer<vec4> grains; } types[3] = {/*Kaolinite*/{0.2, float(Bvx*0.5/*50% air*/*2.60e3), {}},/*Quartz*/{0.6, float(Bvx*2.65e3), {}}, /*Calcite*/{0.2, float(Bvx*2.71e3), {}}};
    const vec3 volumeCenter = vec3(size-int3(1))/2.f;
    const float volumeRadius = volumeCenter.x;
    const float innerRadius = (1-4./100) * volumeRadius;
    const float outerRadius = (1-2./100) * volumeRadius;
    const size_t grainCount = rate*size.z*size.y*size.x;
    vec4 largestGrain = 0;
    buffer<size_t> intersectionCounts;
    buffer<Intersection> intersections;

    /// Generates the explicit lists of grains inside the container fitting a support of size \a size (for each grain type) (to ensure consistenct between \a volume and \a project).
    /// \note Grain intersections are explictly allowed and resolved as inclusions where heavier grains always overrides lighter grains.
    PorousRock(int3 size);
    /// Rasterizes the container cylinder and all grains.
    VolumeF volume();
    /// Analytically projects the container cylinder and all grains along the \a index projection of \a A to \a target.
    float project(const ImageF& target, const Projection& A, uint index);
};

/// Projects \a rock for all projections defined by \a A
inline VolumeF project(PorousRock& rock, const Projection& A) {
    VolumeF volume(A.projectionSize, "b"_);
    //Time time;
    //Time lastReport;
    float minMaxAttenuation=inf, maxMaxAttenuation=-inf;
    for(uint index: range(volume.size.z)) {
        //if(lastReport.toFloat()>1) { log(index); lastReport=Time(); }
        float maxAttenuation = rock.project(::slice(volume, index), A, index);
        minMaxAttenuation = min(minMaxAttenuation, maxAttenuation);
        maxMaxAttenuation = max(maxMaxAttenuation, maxAttenuation);
    }
    //log("A", time, minMaxAttenuation, maxMaxAttenuation);
    return volume;
}
