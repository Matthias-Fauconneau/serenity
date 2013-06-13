/// \file histogram.cc Histograms volume
#include "sample.h"
#include "volume-operation.h"
#include "thread.h"

Sample histogram(const Volume16& source, bool cylinder) {
    int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    assert_(X==Y && marginX==marginY);
    uint radiusSq = cylinder ? (X/2-marginX)*(Y/2-marginY) : -1;
    Sample histogram (source.maximum+1, source.maximum+1, 0);
    for(int z=marginZ; z<Z-marginZ; z++) {
        for(int y=marginY; y<Y-marginY; y++) {
            for(int x=marginX; x<X-marginX; x++) {
                if(uint(sq(x-X/2)+sq(y-Y/2)) <= radiusSq) {
                    uint sample = source(x,y,z);
                    assert(sample <= source.maximum);
                    histogram[sample]++;
                }
            }
        }
    }
    assert(sum(histogram));
    return histogram;
}

/// Computes histogram using uniform integer bins
class(Histogram, Operation) {
    virtual ref<byte> parameters() const { return "cylinder zero"_; } //zero: Whether to include 0 (clipping or background) in the histogram (Defaults to no)
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        Sample histogram = ::histogram(source, args.contains("cylinder"_));
        if(!args.contains("zero"_)) histogram[0] = 0; // Zeroes first value (clipping artifacts or background)
        outputs[0]->metadata = string("histogram.tsv"_);
        outputs[0]->data = toASCII(histogram);
    }
};
