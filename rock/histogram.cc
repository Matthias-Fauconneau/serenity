/// \file histogram.cc Histograms volume and provides Sample methods as Operation
#include "histogram.h"
#include "volume-operation.h"
#include "thread.h"

Sample histogram(const Volume16& source, bool cylinder) {
    int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    assert_(X==Y && marginX==marginY, source.sampleCount, source.margin);
    uint radiusSq = cylinder ? (X/2-marginX)*(Y/2-marginY) : -1;
    Sample histogram (source.maximum+1, source.maximum+1, 0);
    if(source.offsetX || source.offsetY || source.offsetZ) {
        const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
        for(int z=marginZ; z<Z-marginZ; z++) {
            const uint16* sourceZ = source+offsetZ[z];
            for(int y=marginY; y<Y-marginY; y++) {
                const uint16* sourceZY = sourceZ+offsetY[y];
                for(int x=marginX; x<X-marginX; x++) {
                    if(uint((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2)) <= radiusSq) {
                        uint sample = sourceZY[offsetX[x]];
                        assert_(sample <= source.maximum);
                        histogram[sample]++;
                    }
                }
            }
        }
    }
    else {
        for(int z=marginZ; z<Z-marginZ; z++) {
            const uint16* sourceZ = source+z*XY;
            for(int y=marginY; y<Y-marginY; y++) {
                const uint16* sourceZY = sourceZ+y*X;
                for(int x=marginX; x<X-marginX; x++) {
                    if(uint((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2)) <= radiusSq) {
                        uint sample = sourceZY[x];
                        assert_(sample <= source.maximum);
                        histogram[sample]++;
                    }
                }
            }
        }
    }
    return histogram;
}

/// Computes histogram using uniform integer bins
class(Histogram, Operation) {
    virtual ref<byte> parameters() const { return "cylinder"_; }
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        Sample histogram = ::histogram(source, args.contains("cylinder"_));
        outputs[0]->metadata = string("histogram.tsv"_);
        outputs[0]->data = toASCII(histogram);
    }
};

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel
Sample kernelDensityEstimation(const Sample& histogram) {
    const uint N = histogram.size;
    real h = pow(4./(3*N),1./5) * sqrt(histogramVariance(histogram));
    const uint clip = 8192;
    float K[clip]; for(int i: range(clip)) K[i] = exp(-1./2*sq(i)/sq(h))/sqrt(2*PI); // Precomputes gaussian kernel
    Sample pdf ( N );
    parallel(N, [&](uint, uint x0){ // O(N²)
        float sum = 0;
        for(int i: range(min(clip,x0))) sum += histogram[x0-1-i]*K[i];
        for(int i: range(min(clip,uint(histogram.size)-x0))) sum += histogram[x0+i]*K[i];
        pdf[x0] = sum / (::sum(histogram)*h);
    });
    return pdf;
}

class(KernelDensityEstimation, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = string("kde.tsv"_);
        target.data = toASCII(kernelDensityEstimation(parseSample(source.data)));
    }
};

/// Computes histogram, square roots bin values, recounts using uniform integer bins and scales bin values //FIXME: use KDE instead
class(SqrtHistogram, Operation) {
    virtual ref<byte> parameters() const { return "cylinder resolution"_; }
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        Sample squaredHistogram = ::histogram(source, args.contains("cylinder"_));
        squaredHistogram[0] = 0; // Clears background voxel count to plot with a bigger Y scale
        float scale = toDecimal(args.value("resolution"_,"1"_));
        outputs[0]->metadata = string("√histogram.tsv"_);
        outputs[0]->data = toASCII(sqrtHistogram(squaredHistogram), scale);
    }
};

//definePass(Sum, "scalar"_, str(sum(parseSample(source.data))) );
class(Sum, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = string("scalar"_);
        target.data = str(sum(parseSample(source.data)));
    }
};

class(Normalize, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        Sample sample = parseSample(source.data);
        sample[0]=sample[sample.size-1]=0; // Zeroes extreme values (clipping artifacts)
        target.data = toASCII((1./sum(sample))*sample);
    }
};
