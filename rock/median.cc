#include "volume-operation.h"
#include "thread.h"
#include "time.h"

/// Denoises a volume using a 3x3x3 median filter
void median(Volume16& target, const Volume16& source) {
    assert_(!source.offsetX && !source.offsetY && !source.offsetZ);
    const uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const uint marginX=source.margin.x+1, marginY=source.margin.y+1, marginZ=source.margin.z+1;
    const uint16* const sourceData = source;
    uint16* const targetData = target;

    Time time; Time report;
    parallel(marginZ, Z-marginZ, [&](uint id, uint z) {
        if(id==0 && report/1000>=8) log(z,"/", Z-2*marginZ, (z*X*Y/1024./1024.)/(time/1000.), "MS/s"), report.reset();
        for(uint y: range(marginY, Y-marginY)) {
            for(uint x: range(marginX, X-marginX)) { //TODO: vector
                uint origin = z*X*Y+y*X+x;
                constexpr uint N = 3*3*3;
#if 0 // Latency limit - O(N) = O(27) ~ 3 s/GS = 341 MS/s
                uint value=0;
                for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) value += sourceData[origin+dz*X*Y+dy*X+dx]; // Inlines the 3x3x3 window
                targetData[origin] = value/N;
#elif 0 // Insertion - O(N²) = O(729) ~ 80 s/GS = 12.9 MS/s
                uint16 list[N];
                for(int size=0, dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) { // 3x3x3 window
                    uint16 value = sourceData[origin+dz*X*Y+dy*X+dx]; // Inserts each value
                    uint i = size++; // Hole starts at the end of list
                    for(;i>0 && value < list[i-1]; i--) list[i] = list[i-1]; // Shifts hole down while value is smaller
                    list[i] = value;
                }
                targetData[origin] = list[27/2];
#elif 1 // Selection - O(N²) = O(729) ~ 77 s/GS ~ 13.3 MS/s
                uint16 list[N];
                for(int i=0, dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) list[i++] = sourceData[origin+dz*X*Y+dy*X+dx]; // Inlines the 3x3x3 window
                for(uint i=0; i<N; i++) {
                    uint16 value = list[i];
                    uint rank=0; for(uint j=0; j<N; j++) if(list[j]<value) rank++;
                    if(rank==27/2) { targetData[origin] = value; break; }
                }
//#else //TODO: 4-radix O(4×(2^(R/4)+N)) = O(172)
//#else //TODO: O(NlogN) = O(89) ~
#endif
            }
        }
    });
    target.margin = int3(marginX, marginY, marginZ);
}
defineVolumePass(Median, uint16, median);
