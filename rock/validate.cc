#include "volume-operation.h"
#include "thread.h"
#include "time.h"

/// Validates maximum balls results and helps visualizes filter effects
void validate(Volume16& target, const Volume32& pore, const Volume16& maximum) {
    const uint32* const poreData = pore;
    const uint16* const maximumData = maximum;
    uint16* const targetData = target;
    const int X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY = X*Y;
    int marginX=target.margin.x, marginY=target.margin.y, marginZ=target.margin.z;
    assert_(!pore.offsetX && !pore.offsetY && !pore.offsetZ);
    const uint* const offsetX = maximum.offsetX, *offsetY = maximum.offsetY, *offsetZ = maximum.offsetZ;
    assert_(offsetX && offsetY && offsetZ);
    parallel(marginZ,Z-marginZ, [&](uint, uint z) {
        for(int y=marginY; y<Y-marginY; y++) {
            for(int x=marginX; x<X-marginX; x++) {
                uint pore = poreData[z*XY+y*X+x];
                uint max = maximumData[offsetZ[z]+offsetY[y]+offsetX[x]];
                assert(!max || pore == 0xFFFFFFFF, pore, max);
                if(pore == 0xFFFFFFFF && !max) targetData[z*XY+y*X+x] = maximum.maximum; // Show prunned pores
                //targetData[z*XY+y*X+x] = max && max < 4 ? maximum.maximum-max : 0; // Shows only small radii
            }
        }
    } );
    target.squared=true, target.maximum=maximum.maximum;
}

class(Validate, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return 2; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override { validate(outputs[0], inputs[0], inputs[1]); }
};
