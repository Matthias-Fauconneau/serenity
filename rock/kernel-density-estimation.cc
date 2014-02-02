/// \file kernel-density-estimation.cc Estimates probability density functions and wraps some Sample methods as Operation
#include "sample.h"
#include "operation.h"
#include "math.h"
#include "thread.h"

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel
UniformSample kernelDensityEstimation(const UniformHistogram& histogram, real h=nan, bool normalize=false) {
    const real N = histogram.sampleCount();
    if(h==0 || isNaN(h)) h = pow(4./(3*N),1./5) * sqrt(histogram.variance());
    const uint clip = 8192;
    const real scale = (normalize ? real(histogram.size) : 1) / ( sqrt(2*PI) * h /*Normalize kernel (area=1)*/ * (normalize ? N : 1) /*Normalize sampleCount (to density)*/);
    real K[clip]; for(int i: range(clip)) { real x=-1./2*sq(i/h); K[i] = x>expUnderflow ? scale * exp(x) : 0; } // Precomputes gaussian kernel
    UniformSample pdf (histogram.size);
    parallel(pdf.size, [&](uint, uint x0) {
        real sum = 0;
        for(int i: range(1,min(clip,x0))) sum += histogram[x0-i]*K[i];
        for(int i: range(min(clip,uint(histogram.size)-x0))) sum += histogram[x0+i]*K[i];
        pdf[x0] = sum;
    });
    return pdf;
}

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel (on a non uniformly sampled distribution)
UniformSample kernelDensityEstimation(const NonUniformHistogram& histogram, real h=nan, bool normalize=false) {
    const real N = histogram.sampleCount();
    if(h==0 || isNaN(h)) h = pow(4./(3*N),1./5) * sqrt(histogram.variance());
    real max = ::max(histogram.keys) + 4*h;
    real delta = histogram.delta();
    uint sampleCount = align(coreCount, max/delta);
    delta = max/sampleCount;
    UniformSample pdf ( sampleCount );
    pdf.scale = delta;
    const real scale = 1./( sqrt(2*PI) * h /*Normalize kernel (area=1)*/ * (normalize ? N : 1) /*Normalize sampleCount (to density)*/);
    chunk_parallel(pdf.size, [&](uint, uint offset, uint size) { for(uint i : range(offset, offset+size)) {
        const real x0 = i*delta;
        real sum = 0;
        for(auto sample: histogram) { real x = -1./2*sq((x0-sample.key)/h); if(x>expUnderflow) sum += real(sample.value) * exp(x); }
        pdf[i] = scale * sum;
    }});
    return pdf;
}

struct KernelDensityEstimation : Pass {
    virtual string parameters() const { return "ignore-clip bandwidth normalize"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        NonUniformHistogram H;
        if(source.metadata=="vector"_) for(real value: parseVector(source.data)) H.sorted(value)++;
        else H = parseNonUniformSample(source.data); // Histogram
        if(args.value("ignore-clip"_,"0"_)!="0"_) { log("clip"); H.values.first()=H.values.last()=0; } // Ignores clipped values
        bool uniform = true;
        for(uint i: range(H.size())) if(H.keys[i] != i) { uniform=false; break; }
        bool normalize = args.value("normalize"_,"1"_)!="0"_;
        target.data = uniform ?
                    toASCII(kernelDensityEstimation(copy(H.values), fromDecimal(args.value("bandwidth"_)), normalize)) :
                    toASCII(kernelDensityEstimation(H, fromDecimal(args.value("bandwidth"_)), normalize)); // Non uniform KDE
        string xlabel,ylabel; { TextData s(source.metadata); ylabel = s.until('('); xlabel = s.until(')'); }
        target.metadata = (normalize?"σ"_:ylabel)+"("_+xlabel+").tsv"_;
    }
};
template struct Interface<Operation>::Factory<KernelDensityEstimation>;
