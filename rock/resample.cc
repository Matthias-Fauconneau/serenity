#include "volume-operation.h"

/// Downsamples a volume by averaging 2x2x2 samples
void downsample(Volume16& target, const Volume16& source) {
    assert_(!source.tiled());
    int X = source.sampleCount.x, Y = source.sampleCount.y, Z = source.sampleCount.z, XY = X*Y;
    assert_(X%2==0 && Y%2==0 && Z%2==0);
    target.sampleCount = source.sampleCount/2;
    target.data.size /= 8;
    assert(source.margin.x%2==0 && source.margin.y%2==0 && source.margin.z%2==0);
    target.margin = source.margin/2;
    target.maximum=source.maximum;
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    for(int z=0; z<Z/2; z++) {
        const uint16* const sourceZ = sourceData+z*2*XY;
        uint16* const targetZ = targetData+z*XY/2/2;
        for(int y=0; y<Y/2; y++) {
            const uint16* const sourceZY = sourceZ+y*2*X;
            uint16* const targetZY = targetZ+y*X/2;
            for(int x=0; x<X/2; x++) {
                const uint16* const sourceZYX = sourceZY+x*2;
                targetZY[x] =
                        (
                            ( sourceZYX[0*XY+0*X+0] + sourceZYX[0*XY+0*X+1] +
                        sourceZYX[0*XY+1*X+0] + sourceZYX[0*XY+1*X+1]  )
                        +
                        ( sourceZYX[1*XY+0*X+0] + sourceZYX[1*XY+0*X+1] +
                        sourceZYX[1*XY+1*X+0] + sourceZYX[1*XY+1*X+1]  ) ) / 8;
            }
        }
    }
}
defineVolumePass(Downsample, uint16, downsample);

/// Resamples a volume using nearest neighbour (FIXME: linear, cubic)
void resample(Volume16& target, const Volume16& source, int sourceResolution, int targetResolution) {
    double ratio = targetResolution/sourceResolution;
    assert_(ratio > 1./2 && ratio < 1);
    assert_(!source.tiled());
    int X = source.sampleCount.x, Y = source.sampleCount.y, XY = X*Y;
    int3 targetSize = (source.sampleCount-source.margin) * targetResolution / sourceResolution;
    assert_(target.sampleCount == source.sampleCount);
    target.margin = target.sampleCount - targetSize;
    assert_(2*target.margin < target.sampleCount/2);
    target.maximum=source.maximum;
    const uint16* const sourceData = source + source.index(source.margin);
    uint16* const targetData = target + target.index(target.margin);
    for(int z=0; z<targetSize.z; z++) {
        const uint16* const sourceZ = sourceData+(z*sourceResolution/targetResolution)*XY;
        uint16* const targetZ = targetData+z*XY;
        for(int y=0; y<targetSize.y; y++) {
            const uint16* const sourceZY = sourceZ+(y*sourceResolution/targetResolution)*X;
            uint16* const targetZY = targetZ+y*X;
            for(int x=0; x<targetSize.x; x++) {
                targetZY[x] = sourceZY[x*sourceResolution/targetResolution]; // FIXME: linear
            }
        }
    }
}

/// Resamples data
class(Resample, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        int sourceResolution = TextData(otherInputs[0]->data).integer(), targetResolution = TextData(otherInputs[1]->data).integer();
        assert_(targetResolution > sourceResolution); // Supports only downsampling
        const Volume* source = &inputs[0];
        int times = log2(targetResolution/sourceResolution);
        Volume* mipmap[2] = {&outputs[0], &outputs[1]};
        if(times%2) swap(mipmap[0], mipmap[1]);
        for(int unused pass: range(times)) {
            downsample(*mipmap[0], *source);
            sourceResolution *= 2;
            swap(mipmap[0], mipmap[1]);
            source = mipmap[1];
        }
        assert_(mipmap[1]==&outputs[1]);
        assert_(targetResolution > sourceResolution && targetResolution < sourceResolution*2);
        resample(outputs[0], *source, sourceResolution, targetResolution);
    }
};
