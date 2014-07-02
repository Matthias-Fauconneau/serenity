#include "volume-operation.h"

/// Downsamples a volume by averaging 2x2x2 samples
generic void downsample(VolumeT<T>& target, const VolumeT<T>& source) {
    assert_(!source.tiled());
    int X = source.sampleCount.x, Y = source.sampleCount.y, Z = source.sampleCount.z, XY = X*Y;
    assert_(X%2==0 && Y%2==0 && Z%2==0);
    target.sampleCount = source.sampleCount/2;
    target.data.size = source.data.size/8;
    assert(source.margin.x%2==0 && source.margin.y%2==0 && source.margin.z%2==0);
    target.margin = source.margin/2;
    target.maximum=source.maximum;
    const T* const sourceData = source;
    T* const targetData = target;
    for(int z=0; z<Z/2; z++) {
        const T* const sourceZ = sourceData+z*2*XY;
        T* const targetZ = targetData+z*XY/2/2;
        for(int y=0; y<Y/2; y++) {
            const T* const sourceZY = sourceZ+y*2*X;
            T* const targetZY = targetZ+y*X/2;
            for(int x=0; x<X/2; x++) {
                const T* const sourceZYX = sourceZY+x*2;
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

/// Resamples a volume using nearest neighbour (FIXME: linear, cubic)
generic void resample(VolumeT<T>& target, const VolumeT<T>& source, int sourceResolution, int targetResolution) {
    assert_(targetResolution > sourceResolution && targetResolution < sourceResolution*2, sourceResolution, targetResolution);
    assert_(!source.tiled());
    int X = source.sampleCount.x, Y = source.sampleCount.y, XY = X*Y;
    int3 targetSize = (source.sampleCount-source.margin) * sourceResolution / targetResolution;
    assert_(source.sampleCount <= target.sampleCount);
    target.sampleCount = source.sampleCount;
    assert_(target.size() * target.sampleSize <= target.data.size);
    target.data.size = target.size() * target.sampleSize;
    target.margin = (target.sampleCount - targetSize)/2;
    assert_(target.margin >= int3(0) && 2*target.margin < target.sampleCount/2, target.sampleCount, targetSize, target.margin);
    target.maximum=source.maximum;
    const T* const sourceData = source + source.index(source.margin);
    T* const targetData = target + target.index(target.margin);
    for(int z=0; z<targetSize.z; z++) {
        const T* const sourceZ = sourceData+(z*sourceResolution/targetResolution)*XY;
        T* const targetZ = targetData+z*XY;
        for(int y=0; y<targetSize.y; y++) {
            const T* const sourceZY = sourceZ+(y*sourceResolution/targetResolution)*X;
            T* const targetZY = targetZ+y*X;
            for(int x=0; x<targetSize.x; x++) {
                targetZY[x] = sourceZY[x*sourceResolution/targetResolution]; // FIXME: linear
            }
        }
    }
}

/// Resamples data
struct Resample : VolumeOperation {
    uint outputSampleSize(const Dict&, const ref<const Result*>& inputs, uint) override { return toVolume(*inputs[0]).sampleSize; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<const Result*>& otherInputs) override {
        uint sourceResolution = round(TextData(otherInputs[0]->data).decimal()*1000), targetResolution = round(TextData(otherInputs[1]->data).decimal()*1000);
        if(sourceResolution == targetResolution) { copy(outputs[0].data, inputs[0].data); return; }
        assert_(targetResolution > sourceResolution, targetResolution, sourceResolution); // Supports only downsampling
        const Volume* source = &inputs[0];
        int times = log2(targetResolution/sourceResolution);
        Volume* mipmap[2] = {&outputs[0], &outputs[1]};
        if(times%2) swap(mipmap[0], mipmap[1]);
        if(targetResolution==(sourceResolution<<times)) swap(mipmap[0], mipmap[1]);
        for(int unused pass: range(times)) {
            /**/  if(source->sampleSize==sizeof(uint8)) downsample<uint8>(*mipmap[0], *source);
            else if(source->sampleSize==sizeof(uint16)) downsample<uint16>(*mipmap[0], *source);
            else error(source->sampleSize);
            sourceResolution *= 2;
            swap(mipmap[0], mipmap[1]);
            source = mipmap[1];
        }
        if(sourceResolution == targetResolution) assert_(mipmap[1]==&outputs[0]);
        else {
            assert_(mipmap[1]==&outputs[1]);
            if(source->sampleSize==sizeof(uint8)) resample<uint8>(outputs[0], *source, sourceResolution, targetResolution);
            else if(source->sampleSize==sizeof(uint16)) resample<uint16>(outputs[0], *source, sourceResolution, targetResolution);
            else error(source->sampleSize);
        }
        assert_(outputs[0].data.size == outputs[0].size() * outputs[0].sampleSize, outputs[0].size(), outputs[0].sampleSize, outputs[0].sampleCount, outputs[0].data.size, outputs[0].size() * outputs[0].sampleSize);
    }
};
template struct Interface<Operation>::Factory<Resample>;
