/// \file histogram.cc Histograms volume
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"
#include "crop.h"

generic UniformHistogram histogram(const VolumeT<T>& source, CropVolume crop) {
    assert_(crop.min>=source.margin && crop.max <= source.sampleCount-source.margin, source.margin, crop.min, crop.max, source.sampleCount-source.margin);
    uint radiusSq = crop.cylinder ? sq(crop.size.x/2) : -1;
    int2 center = ((crop.min+crop.max)/2).xy();
    bool tiled=source.tiled();
    const ref<uint64> offsetX = source.offsetX, offsetY = source.offsetY, offsetZ = source.offsetZ;
    const T* sourceData = source;
    buffer<uint> histograms[coreCount];
    for(uint id: range(coreCount)) histograms[id] = buffer<uint>(source.maximum+1, source.maximum+1, 0);
    parallel(crop.min.z, crop.max.z, [&](uint id, uint z) {
        uint* const histogram = histograms[id].begin();
        if(tiled) {
            const T* sourceZ = sourceData + offsetZ[z];
            for(int y=crop.min.y; y<crop.max.y; y++) {
                const T* sourceZY = sourceZ + offsetY[y];
                for(int x=crop.min.x; x<crop.max.x; x++) {
                    const T* sourceZYX = sourceZY + offsetX[x];
                    if(uint(sq(x-center.x)+sq(y-center.y)) <= radiusSq) {
                        uint sample = sourceZYX[0];
                        assert_(sample <= source.maximum, sample, source.maximum);
                        histogram[sample]++;
                    }
                }
            }
        } else {
            const uint64 X=source.sampleCount.x, Y=source.sampleCount.y;
            const T* sourceZ = sourceData + z*X*Y;
            for(int y=crop.min.y; y<crop.max.y; y++) {
                const T* sourceZY = sourceZ + y*X;
                for(int x=crop.min.x; x<crop.max.x; x++) {
                    const T* sourceZYX = sourceZY + x;
                    if(uint(sq(x-center.x)+sq(y-center.y)) <= radiusSq) {
                        uint sample = sourceZYX[0];
                        assert_(sample <= source.maximum, sample, source.maximum);
                        histogram[sample]++;
                    }
                }
            }
        }
    });
    UniformHistogram histogram (source.maximum+1);
    for(uint value: range(source.maximum+1)) { // Merges histograms (and converts to floating point)
        histogram[value] = 0;
        for(uint id: range(coreCount)) histogram[value] += histograms[id][value];
    }
    return histogram;
}

/// Computes histogram using uniform integer bins
struct Histogram : Operation {
    string parameters() const override { return "cylinder downsample"_; }
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        CropVolume crop = parseCrop(args, source.origin+source.margin, source.origin+source.sampleCount-source.margin);
        crop.min -= source.origin, crop.max -= source.origin;
        UniformHistogram histogram;
        if(source.sampleSize==sizeof(uint8)) histogram = ::histogram<uint8>(source, crop);
        else if(source.sampleSize==sizeof(uint16)) histogram = ::histogram<uint16>(source, crop);
        else error(source.sampleSize);
        outputs[0]->metadata = String("V("_+source.field+").tsv"_);
        outputs[0]->data = toASCII(histogram);
    }
};
template struct Interface<Operation>::Factory<Histogram>;
