#pragma once
#include "volume.h"
#include "data.h"

typedef array<uint> Histogram;

/// Computes histogram of values (without normalizing)
Histogram histogram(const Volume16& volume) {
    uint X=volume.x, Y=volume.y, Z=volume.z, XY=X*Y;
    uint marginX=volume.marginX, marginY=volume.marginY, marginZ=volume.marginZ;
    assert(X==Y && marginX==marginY);
    uint radiusSq=(X/2-marginX)*(X/2-marginX);
    Histogram histogram (volume.maximum+1, volume.maximum+1, 0);
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint16* sourceZ = volume+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* sourceY = sourceZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                if((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2) >= radiusSq) continue; // Clips to cylinder
                uint density = sourceY[x];
                histogram[density]++;
            }
        }
    }
    return histogram;
}

/// Computes histogram of square roots (without normalizing)
Histogram sqrtHistogram(const Volume16& volume) {
    uint X=volume.x, Y=volume.y, Z=volume.z, XY=X*Y;
    uint marginX=volume.marginX, marginY=volume.marginY, marginZ=volume.marginZ;
    assert(X==Y && marginX==marginY);
    uint radiusSq=(X/2-marginX)*(X/2-marginX);
    uint maximum = round(sqrt(volume.maximum))+1;
    Histogram histogram (maximum, maximum, 0);
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint16* sourceZ = volume+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* sourceY = sourceZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                if((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2) >= radiusSq) continue; // Clips to cylinder
                uint radius = round(sqrt(sourceY[x]));
                histogram[radius]++;
            }
        }
    }
    return histogram;
}

Histogram parseHistogram(const ref<byte>& file) {
    Histogram histogram;
    TextData s (file);
    while(s) { uint i=s.integer(); histogram.grow(i+1); s.skip("\t"_); histogram[i]=s.integer(); s.skip("\n"_); }
    return histogram;
}

inline float ceil(float x) { return __builtin_ceilf(x); }
inline float log10(float x) { return __builtin_log10f(x); }
string str(const Histogram& histogram, float scale=1) {
    string s;
    for(uint i=0; i<histogram.size; i++) if(histogram[i]) s << ftoa(i*scale,ceil(-log10(scale))) << '\t' << str(histogram[i]) << '\n';
    return s;
}
