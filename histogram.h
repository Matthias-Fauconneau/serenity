#pragma once
#include "volume.h"
#include "data.h"

struct Histogram {
    static constexpr uint binCount=0x100;
    uint count[binCount] = {};
};

Histogram histogram(const Volume16& volume) {
    uint X=volume.x, Y=volume.y, Z=volume.z, XY=X*Y;
    uint marginX=volume.marginX, marginY=volume.marginY, marginZ=volume.marginZ;
    uint radiusSq=(X/2-marginX)*(X/2-marginX);
    Histogram histogram;
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint16* sourceZ = volume+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* sourceY = sourceZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                if((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2) >= radiusSq) continue;
                uint density = sourceY[x]*(0x100*volume.num)/volume.den; // Downscale to 8bit
                assert(density<0x100);
                histogram.count[density]++;
            }
        }
    }
    return histogram;
}

Histogram sqrtHistogram(const Volume32& volume) {
    uint X=volume.x, Y=volume.y, Z=volume.z, XY=X*Y;
    uint marginX=volume.marginX, marginY=volume.marginY, marginZ=volume.marginZ;
    uint radiusSq=(X/2-marginX)*(X/2-marginX);
    Histogram histogram;
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint32* sourceZ = volume+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint32* sourceY = sourceZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                if((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2) >= radiusSq) continue;
                uint distance = sqrt(sourceY[x]);
                if(distance<0xFF) histogram.count[distance]++;
            }
        }
    }
    return histogram;
}

Histogram parseHistogram(const ref<byte>& file) {
    Histogram histogram;
    TextData s (file);
    for(uint i=0; i<histogram.binCount; i++) s.skip(str(i)), s.skip("\t"_), histogram.count[i]=s.integer(), s.skip("\n"_);
    return histogram;
}

string str(const Histogram& histogram) {
    string s;
    for(uint i=0; i<histogram.binCount; i++) s << str(i) << '\t' << str(histogram.count[i]) << '\n';
    return s;
}
