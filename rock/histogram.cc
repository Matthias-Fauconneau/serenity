/// \file histogram.cc Histograms volume
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"
#include "crop.h"

UniformHistogram histogram(const Volume16& source, CropVolume crop) {
    uint X=source.sampleCount.x, Y=source.sampleCount.y;
    uint radiusSq = crop.cylinder ? sq(crop.size.x/2) : -1;
    int2 center = ((crop.min+crop.max)/2).xy();
    bool tiled=source.tiled();
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    const uint16* sourceData = source;
    buffer<uint> histograms[coreCount];
    for(uint id: range(coreCount)) histograms[id] = buffer<uint>(source.maximum+1, source.maximum+1, 0);
    parallel(crop.min.z, crop.max.z, [&](uint id, uint z) {
        uint* const histogram = histograms[id].begin();
        if(tiled) {
            const uint16* sourceZ = sourceData + offsetZ[z];
            for(int y=crop.min.y; y<crop.max.y; y++) {
                const uint16* sourceZY = sourceZ + offsetY[y];
                for(int x=crop.min.x; x<crop.max.x; x++) {
                    const uint16* sourceZYX = sourceZY + offsetX[x];
                    if(uint(sq(x-center.x)+sq(y-center.y)) <= radiusSq) {
                        uint sample = sourceZYX[0];
                        assert(sample <= source.maximum);
                        histogram[sample]++;
                    }
                }
            }
        } else {
            const uint16* sourceZ = sourceData + z*X*Y;
            for(int y=crop.min.y; y<crop.max.y; y++) {
                const uint16* sourceZY = sourceZ + y*X;
                for(int x=crop.min.x; x<crop.max.x; x++) {
                    const uint16* sourceZYX = sourceZY + x;
                    if(uint(sq(x-center.x)+sq(y-center.y)) <= radiusSq) {
                        uint sample = sourceZYX[0];
                        assert(sample <= source.maximum);
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
class(Histogram, Operation) {
    virtual string parameters() const { return "crop cylinder"_; }
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        UniformHistogram histogram = ::histogram(source, parseCrop(args.contains("crop"_)?args:(const Dict&)Dict(), source.margin, source.sampleCount-source.margin, ""_, 1));
        outputs[0]->metadata = String("histogram.tsv"_);
        outputs[0]->data = toASCII(histogram);
    }
};

class(Normalize, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        auto sample = parseUniformSample(source.data);
        //NonUniformSample sample = parseNonUniformSample(source.data);
        float sum = sample.sum();
        assert_(sum);
        target.data = toASCII((1./sum)*sample);
    }
};
