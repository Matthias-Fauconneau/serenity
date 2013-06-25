/// \file histogram.cc Histograms volume
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"

UniformHistogram histogram(const Volume16& source, bool cylinder) {
    uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    assert_(X==Y && marginX==marginY);
    uint radiusSq = cylinder ? (X/2-marginX)*(Y/2-marginY) : -1;
    bool tiled=source.tiled();
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    const uint16* sourceData = source;
    buffer<uint> histograms[coreCount];
    for(uint id: range(coreCount)) histograms[id] = buffer<uint>(source.maximum+1, source.maximum+1, 0);
    parallel(marginZ, Z-marginZ, [&](uint id, uint z) {
        for(uint y=marginY; y<Y-marginY; y++) {
            for(uint x=marginX; x<X-marginX; x++) {
                if(uint(sq(x-X/2)+sq(y-Y/2)) <= radiusSq) {
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
    } return histogram;
}

/// Computes histogram using uniform integer bins
class(Histogram, Operation) {
    virtual string parameters() const { return "cylinder clip"_; }
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        UniformHistogram histogram = ::histogram(source, args.contains("cylinder"_));
        uint clip = args.value("clip"_,1);
        for(uint i: range(clip)) histogram[i] = 0; // Zeroes values until clip (discards clipping artifacts or background)
        outputs[0]->metadata = String("histogram.tsv"_);
        outputs[0]->data = clip == histogram.size ? String() : toASCII(histogram);
    }
};

/// Zeroes first clip values
class(Zero, Operation), virtual Pass {
    virtual string parameters() const { return "clip"_; }
    virtual void execute(const Dict& args, Result& output, const Result& source) override {
        UniformSample sample = parseUniformSample(source.data);
        uint clip = args.value("clip"_,1);
        for(uint i: range(clip)) sample[i] = 0; // Zeroes values until clip (discards clipping artifacts or background)
        output.metadata = copy(source.metadata);
        output.data = toASCII(sample);
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
