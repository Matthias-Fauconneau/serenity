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
    histogram[0] = 0; // Zeroes first value (clipping artifacts or background)
    return histogram;
}

/// Computes histogram using uniform integer bins
class(Histogram, Operation) {
    virtual ref<byte> parameters() const { return "cylinder"_; } //FIXME: use ClipCylinder
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        Sample histogram = ::histogram(source, args.contains("cylinder"_));
        outputs[0]->metadata = string("histogram.tsv"_);
        outputs[0]->data = toASCII(histogram);
    }
};

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel
Sample kernelDensityEstimation(const Sample& histogram) {
    const real N = ::sum(histogram);
    real h = pow(4./(3*N),1./5) * sqrt(histogramVariance(histogram));
    const uint clip = 8192;
    float K[clip]; for(int i: range(clip)) { float x=-1./2*sq(i/h); K[i] = x>expUnderflow?exp(x)/sqrt(2*PI) : 0; } // Precomputes gaussian kernel
    Sample pdf (histogram.size);
    parallel(histogram.size, [&](uint, uint x0) {
        float sum = 0;
        for(int i: range(min(clip,x0))) sum += histogram[x0-1-i]*K[i];
        for(int i: range(min(clip,uint(histogram.size)-x0))) sum += histogram[x0+i]*K[i];
        pdf[x0] = sum / (N*h);
    });
    return pdf;
}

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel (on a non uniformly sampled distribution)
NonUniformSample kernelDensityEstimation(const NonUniformSample& histogram) {
    const real N = ::sum(histogram);
    real h = pow(4./(3*N),1./5) * sqrt(histogramVariance(histogram));
    NonUniformSample pdf = copy(histogram);
    parallel(histogram.size(), [&](uint, uint i) {
        const float x0 = histogram.keys[i];
        float sum = 0;
        for(auto sample: histogram) if((x0-sample.key)<h) { real x=-1./2*sq((x0-sample.key)/h); if(x>expUnderflow) sum += sample.value * exp(x)/sqrt(2*PI); }
        pdf.values[i] = sum / (N*h);
    });
    pdf = (1./sum(pdf))*pdf; // FIXME
    return pdf;
}

class(KernelDensityEstimation, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = string("kde.tsv"_);
        NonUniformSample sample = parseNonUniformSample(source.data);
        UniformSample uniformSample = toUniformSample(sample);
        target.data = uniformSample ? toASCII(kernelDensityEstimation(uniformSample)) : toASCII(kernelDensityEstimation(sample));
    }
};

//definePass(Sum, "scalar"_, str(sum(parseSample(source.data))) );
class(Sum, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = string("scalar"_);
        target.data = str(sum(parseSample(source.data)));
    }
};

#if 1 // Superseded by KDE (for KDE debugging purpose only)
class(Normalize, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        NonUniformSample sample = parseNonUniformSample(source.data);
        sample.values[0]=sample.values[sample.size()-1]=0; // Zeroes extreme values (clipping artifacts)
        target.data = toASCII((1./sum(sample))*sample);
    }
};
#endif

/// Square roots the variable of a distribution
class(SquareRootVariable, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        target.data = toASCII(squareRootVariable(parseNonUniformSample(source.data)));
    }
};

/// Scales the variable of a distribution
class(ScaleVariable, Operation), virtual Pass {
    virtual ref<byte> parameters() const { return "scale"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        target.data = toASCII(scaleVariable(toDecimal(args.at("scale"_)), parseNonUniformSample(source.data)));
    }
};
