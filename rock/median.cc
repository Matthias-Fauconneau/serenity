#include "volume-operation.h"
#include "thread.h"
#include "time.h"

/// Denoises a volume using a 3x3x3 median filter
void median(Volume16& target, const Volume16& source) {
    assert_(!source.offsetX && !source.offsetY && !source.offsetZ);
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const int marginX=source.margin.x+1, marginY=source.margin.y+1, marginZ=source.margin.z+1;
    const uint16* const sourceData = source;
    uint16* const targetData = target;

    Time time; Time report;
    parallel(marginZ, Z-marginZ, [&](uint id, uint z) {
        if(id==0 && report/1000>=8) log(z,"/", Z-2*marginZ, (z*X*Y/1024./1024.)/(time/1000.), "MS/s"), report.reset();
        for(uint y: range(marginY, Y-marginY)) {
            //TODO: histogram4
            uint8 histogram8[1<<8] = {};
            //TODO: histogram12
            uint8 histogram16[1<<16] = {}; //64K
            const uint16* const sourceZX = sourceData + z*X*Y + y*X;
            for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int x=marginX-1; x<=marginX+1; x++) { // Initializes histograms
                uint16 value = sourceZX[dz*X*Y+dy*X+x];
                histogram8[value>>8]++;
                histogram16[value]++;
            }
            for(int x: range(marginX, X-marginX)) { //TODO: vector
                for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) { // Updates histogram with values entering the window
                    uint16 value = sourceZX[dz*X*Y+dy*X+x+1];
                    histogram8[value>>8]++;
                    histogram16[value]++;
                }
                for(uint count=0, i=0; i<256; i++) { // Coarse level
                    uint after = count + histogram8[i];
                    if(after > 27/2) {
                        for(uint j=0; j<256; j++) { // Fine level
                            count += histogram16[j];
                            targetData[z*X*Y+y*X+x] = (i<<8) + j;
                            break;
                        }
                        break;
                    }
                    count = after;
                }
                for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) { // Updates histogram with values leaving the window
                    uint16 value = sourceZX[dz*X*Y+dy*X+x-1];
                    histogram8[value>>8]--;
                    histogram16[value]--;
                }
            }
        }
    });
    target.margin = int3(marginX, marginY, marginZ);
}
defineVolumePass(Median, uint16, median);
