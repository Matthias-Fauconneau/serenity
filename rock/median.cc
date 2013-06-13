#include "volume-operation.h"
#include "thread.h"
#include "time.h"

/// Denoises a volume using a 3x3x3 median filter
void median(Volume16& target, const Volume16& source) {
    assert_(!source.tiled());
    const int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const int marginX=source.margin.x+1, marginY=source.margin.y+1, marginZ=source.margin.z+1;
    target.margin = int3(marginX, marginY, marginZ);
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    uint maximum[8] = {};
    Time time; Time report;
    parallel(marginZ, Z-marginZ, [&](uint id, uint z) {
        if(id==0 && report/1000>=8) log_("\t"_+str(z,"/", Z-2*marginZ, (z*X*Y/1024./1024.)/(time/1000.), "MS/s")), report.reset();
        for(uint y: range(marginY, Y-marginY)) {
            uint8 histogram4[1<<4] = {};
            uint8 histogram8[1<<8] = {};
            uint8 histogram12[1<<12] = {}; //4K
            uint8 histogram16[1<<16] = {}; //64K
            const uint16* const sourceZX = sourceData + z*X*Y + y*X;
            for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int x=marginX-1; x<marginX+1; x++) { // Initializes histograms
                uint16 value = sourceZX[dz*X*Y+dy*X+x];
                histogram4[value>>12]++, histogram8[value>>8]++, histogram12[value>>4]++, histogram16[value]++;
            }
            for(int x: range(marginX, X-marginX)) { //TODO: vector
                for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) { // Updates histogram with values entering the window
                    uint16 value = sourceZX[dz*X*Y+dy*X+x+1];
                    histogram4[value>>12]++, histogram8[value>>8]++, histogram12[value>>4]++, histogram16[value]++;
                }
                uint count=0;
                uint i=0; for(; i<16; i++) { uint after = count + histogram4[i]; if(after > 27/2) break; count = after; } uint offset = i<<4;
                uint j=0; for(; j<16; j++) { uint after = count + histogram8[offset+j]; if(after > 27/2) break; count = after; } offset = (offset+j)<<4;
                uint k=0; for(; k<16; k++) { uint after = count + histogram12[offset+k]; if(after > 27/2) break; count = after; } offset = (offset+k)<<4;
                for(uint l=0; l<16; l++) {
                    count += histogram16[offset+l];
                    if(count > 27/2) {
                        uint value = (i<<12) + (j<<8) + (k<<4) + l;
                        targetData[z*X*Y+y*X+x] = value;
                        if(value > maximum[id]) maximum[id] = value;
                        break;
                    }
                }
                for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) { // Updates histogram with values leaving the window
                    uint16 value = sourceZX[dz*X*Y+dy*X+x-1];
                    histogram4[value>>12]--, histogram8[value>>8]--, histogram12[value>>4]--, histogram16[value]--;
                }
            }
        }
    });
    target.maximum = max(ref<uint>(maximum));
}
defineVolumePass(Median, uint16, median);
