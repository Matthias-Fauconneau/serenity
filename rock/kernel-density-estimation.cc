/// \file kernel-density-estimation.cc Estimates probability density functions and wraps some Sample methods as Operation
#include "sample.h"
#include "operation.h"
#include "math.h"
#include "thread.h"

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel
Sample kernelDensityEstimation(const Sample& histogram) {
    const float N = ::sum(histogram);
    float h = pow(4./(3*N),1./5) * sqrt(histogramVariance(histogram));
    const uint clip = 8192;
    float K[clip]; for(int i: range(clip)) { float x=-1./2*sq(i/h); K[i] = x>expUnderflow?exp(x)/sqrt(2*PI) : 0; } // Precomputes gaussian kernel
    Sample pdf (histogram.size);
    parallel(pdf.size, [&](uint, uint x0) {
        float sum = 0;
        for(int i: range(min(clip,x0))) sum += histogram[x0-1-i]*K[i];
        for(int i: range(min(clip,uint(histogram.size)-x0))) sum += histogram[x0+i]*K[i];
        pdf[x0] = sum / (N*h);
    });
    return pdf;
}

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel (on a non uniformly sampled distribution)
NonUniformSample kernelDensityEstimation(const NonUniformSample& histogram, float h=nan) {
    const float N = ::sum(histogram);
    if(h==0 || isNaN(h)) h = pow(4./(3*N),1./5) * sqrt(histogramVariance(histogram));
    float max = ::max(histogram.keys);
    float delta=__FLT_MAX__;
    for(uint i: range(histogram.keys.size-1)) {
        float diff = histogram.keys[i+1]-histogram.keys[i];
        assert_(diff>0, histogram.keys[i], histogram.keys[i+1]);
        delta=min(delta, diff);
    }
    uint sampleCount = max/delta;
    UniformSample pdf ( sampleCount );
    parallel(pdf.size, [&](uint, uint i) {
        const float x0 = i*delta;
        float sum = 0;
        for(auto sample: histogram) sum += sample.value * exp(-1./2*sq((x0-sample.key)/h))/sqrt(2*PI);
        pdf[i] = sum / (N*h);
    });
    return scaleVariable(max/sampleCount, toNonUniformSample(pdf)); //FIXME: implement scaled UniformSample
}

class(KernelDensityEstimation, Operation), virtual Pass {
    virtual ref<byte> parameters() const { return "bandwidth"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        target.metadata = string("kde.tsv"_);
        NonUniformSample sample = parseNonUniformSample(source.data);
        UniformSample uniformSample = toUniformSample(sample);
        target.data = uniformSample ? toASCII(kernelDensityEstimation(uniformSample)) : toASCII(kernelDensityEstimation(sample, toDecimal(args.value("bandwidth"_))));
    }
};

//definePass(Sum, "scalar"_, str(sum(parseUniformSample(source.data))) );
class(Sum, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = string("scalar"_);
        target.data = str(sum(parseUniformSample(source.data)))+"\n"_;
    }
};

/// Square roots the variable of a distribution
class(SquareRootVariable, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_), "Expected a distribution, not a", source.metadata, source.name, target.name);
        target.metadata = copy(source.metadata);
        target.data = toASCII(squareRootVariable(parseNonUniformSample(source.data)));
    }
};

/// Scales the variable of a distribution
class(ScaleVariable, Operation), virtual Pass {
    virtual ref<byte> parameters() const { return "scale"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_), "Expected a distribution, not a", source.metadata, source.name, target.name);
        target.metadata = copy(source.metadata);
        target.data = toASCII(scaleVariable(toDecimal(args.at("scale"_)), parseNonUniformSample(source.data)));
    }
};

/// Divides two scalars
class(Div, Operation) {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        outputs[0]->metadata = copy(inputs[0]->metadata);
        if(inputs[0]->metadata=="scalar"_) outputs[0]->data = ftoa(TextData(inputs[0]->data).decimal()/TextData(inputs[1]->data).decimal(), 4)+"\n"_;
        else if(endsWith(inputs[0]->metadata,".tsv"_)) outputs[0]->data = toASCII( (1./TextData(inputs[1]->data).decimal()) * parseNonUniformSample(inputs[0]->data) );
    }
};
