#include "volume-operation.h"
#include "thread.h"
#include "time.h"

/// Denoises a volume using a 3x3x3 median filter
void median(Volume16& target, const Volume16& source) {
    assert_(!source.tiled());
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const int marginX=source.margin.x+1, marginY=source.margin.y+1, marginZ=source.margin.z+1;
    const uint16* const sourceData = source;
    uint16* const targetData = target;

    Time time; Time report;
    parallel(marginZ, Z-marginZ, [&](uint id, uint z) {
        if(id==0 && report/1000>=8) log(z,"/", Z-2*marginZ, (z*X*Y/1024./1024.)/(time/1000.), "MS/s"), report.reset();
        for(uint y: range(marginY, Y-marginY)) {
            uint8 histogram4[1<<4] = {};
            uint8 histogram8[1<<8] = {};
            uint8 histogram12[1<<12] = {}; //4K
            uint8 histogram16[1<<16] = {}; //64K
            const uint16* const sourceZX = sourceData + z*X*Y + y*X;
            for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int x=marginX-1; x<=marginX+1; x++) { // Initializes histograms
                uint16 value = sourceZX[dz*X*Y+dy*X+x];
                histogram4[value>>12]++, histogram8[value>>8]++, histogram12[value>>4]++, histogram16[value]++;
            }
            for(int x: range(marginX, X-marginX)) { //TODO: vector
                for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) { // Updates histogram with values entering the window
                    uint16 value = sourceZX[dz*X*Y+dy*X+x+1];
                    histogram4[value>>12]++, histogram8[value>>8]++, histogram12[value>>4]++, histogram16[value]++;
                }
                for(uint count=0, i=0; i<16; i++) { // histogram4
                    uint after = count + histogram4[i];
                    if(after > 27/2) {
                        for(uint j=0; j<16; j++) { // histogram8
                            uint after = count + histogram8[j];
                            if(after > 27/2) {
                                for(uint k=0; k<16; k++) { // histogram12
                                    uint after = count + histogram12[k];
                                    if(after > 27/2) {
                                        for(uint l=0; l<16; l++) { // histogram16
                                            count += histogram16[l];
                                            if(count > 27/2) {
                                                targetData[z*X*Y+y*X+x] = (i<<12) + (j<<8) + (k<<4) + l;
                                                break;
                                            }
                                        }
                                        break;
                                    }
                                }
                                break;
                            }
                            count = after;
                        }
                        break;
                    }
                    count = after;
                }
                for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) { // Updates histogram with values leaving the window
                    uint16 value = sourceZX[dz*X*Y+dy*X+x-1];
                    histogram4[value>>12]--, histogram8[value>>8]--, histogram12[value>>4]--, histogram16[value]--;
                }
            }
        }
    });
    target.margin = int3(marginX, marginY, marginZ);
}
defineVolumePass(Median, uint16, median);
