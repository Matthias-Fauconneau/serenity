/// \file kernel-density-estimation.cc Estimates probability density functions and wraps some Sample methods as Operation
#include "sample.h"
#include "operation.h"
#include "math.h"
#include "thread.h"

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel
UniformSample<double> kernelDensityEstimation(const UniformHistogram& histogram, double h=nan, bool normalize=false) {
    const double N = histogram.sampleCount();
    if(h==0 || isNaN(h)) h = pow(4./(3*N),1./5) * sqrt(histogram.variance());
    const uint clip = 8192;
    const double scale = 1./( sqrt(2*PI) * h /*Normalize kernel (area=1)*/ * (normalize ? N : 1) /*Normalize sampleCount (to density)*/);
    double K[clip]; for(int i: range(clip)) { double x=-1./2*sq(i/h); K[i] = x>expUnderflow ? scale * exp(x) : 0; } // Precomputes gaussian kernel
    UniformSample<double> pdf (histogram.size);
    parallel(pdf.size, [&](uint, uint x0) {
        double sum = 0;
        for(int i: range(min(clip,x0))) sum += histogram[x0-1-i]*K[i];
        for(int i: range(min(clip,uint(histogram.size)-x0))) sum += histogram[x0+i]*K[i];
        pdf[x0] = sum;
    });
    return pdf;
}

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel (on a non uniformly sampled distribution)
UniformSample<double> kernelDensityEstimation(const NonUniformHistogram& histogram, double h=nan, bool normalize=false) {
    const double N = histogram.sampleCount();
    if(h==0 || isNaN(h)) h = pow(4./(3*N),1./5) * sqrt(histogram.variance());
    double max = ::max(histogram.keys) + 4*h;
    double delta=__FLT_MAX__;
    for(uint i: range(histogram.keys.size-1)) {
        double diff = histogram.keys[i+1]-histogram.keys[i];
        assert_(diff>0, histogram.keys[i], histogram.keys[i+1]);
        delta=min(delta, diff);
    }
    uint sampleCount = align(coreCount, max/delta);
    delta = max/sampleCount;
    UniformSample<double> pdf ( sampleCount );
    pdf.scale = delta;
    const double scale = 1./( sqrt(2*PI) * h /*Normalize kernel (area=1)*/ * (normalize ? N : 1) /*Normalize sampleCount (to density)*/);
    chunk_parallel(pdf.size, [&](uint offset, uint size) { for(uint i : range(offset, offset+size)) {
        const double x0 = i*delta;
        double sum = 0;
        for(auto sample: histogram) sum += double(sample.value) * exp(-1./2*sq((x0-sample.key)/h));
        pdf[i] = scale * sum;
    }});
    return pdf;
}

class(KernelDensityEstimation, Operation), virtual Pass {
    virtual string parameters() const { return "bandwidth normalize"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        target.metadata = String("kde.tsv"_);
        NonUniformHistogram H = parseNonUniformSample<double,int64>(source.data);
        bool uniform = true;
        for(uint i: range(H.size())) if(H.keys[i] != i) { uniform=false; break; }
        target.data = uniform ?
                    toASCII(kernelDensityEstimation(copy(H.values), toDecimal(args.value("bandwidth"_)), args.value("normalize"_,"0"_)!="0"_)) :
                    toASCII(kernelDensityEstimation(H, toDecimal(args.value("bandwidth"_)), args.value("normalize"_,"0"_)!="0"_)); // Non uniform KDE
    }
};
