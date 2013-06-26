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
        for(int y=crop.min.y; y<crop.max.y; y++) {
            for(int x=crop.min.x; x<crop.max.x; x++) {
                if(uint(sq(x-center.x)+sq(y-center.y)) <= radiusSq) {
                    uint sample = sourceData[ tiled ? (offsetX[x]+offsetY[y]+offsetZ[z]) : (z*X*Y+y*X+x) ];
                    assert_(sample <= source.maximum, source.margin, x,y,z, source.sampleCount-source.margin, source.sampleCount, sample, source.maximum);
                    histograms[id][sample]++;
                }
            }
        }
    });
    UniformHistogram histogram (source.maximum+1);
    for(uint value: range(source.maximum+1)) { // Merges histograms (and converts to float)
        histogram[value] = 0;
        for(uint id: range(coreCount)) histogram[value] += histograms[id][value];
    }
    assert_(histogram.sum());
    return histogram;
}

/// Computes histogram using uniform integer bins
class(Histogram, Operation) {
    virtual string parameters() const { return "cylinder clip"_; }
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        UniformHistogram histogram = ::histogram(source, parseCrop(args, source.margin, source.sampleCount-source.margin, ""_, 1));
        uint clip = args.value("clip"_,1);
        for(uint i: range(clip)) histogram[i] = 0; // Zeroes values until clip (discards clipping artifacts or background)
        outputs[0]->metadata = String("histogram.tsv"_);
        outputs[0]->data = clip == histogram.size ? String() : toASCII(histogram);
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
