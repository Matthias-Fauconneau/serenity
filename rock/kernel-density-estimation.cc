/// \file kernel-density-estimation.cc Estimates probability density functions and wraps some Sample methods as Operation
#include "sample.h"
#include "operation.h"
#include "math.h"
#include "thread.h"

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel
UniformSample<double> kernelDensityEstimation(const UniformHistogram& histogram) {
    const double N = histogram.sampleCount();
    double h = pow(4./(3*N),1./5) * sqrt(histogram.variance());
    const uint clip = 8192;
    double K[clip]; for(int i: range(clip)) { float x=-1./2*sq(i/h); K[i] = x>expUnderflow?exp(x)/sqrt(2*PI) : 0; } // Precomputes gaussian kernel
    UniformSample<double> pdf (histogram.size);
    parallel(pdf.size, [&](uint, uint x0) {
        double sum = 0;
        for(int i: range(min(clip,x0))) sum += histogram[x0-1-i]*K[i];
        for(int i: range(min(clip,uint(histogram.size)-x0))) sum += histogram[x0+i]*K[i];
        pdf[x0] = sum / (N*h);
    });
    return pdf;
}

/// Samples the probability density function estimated from an histogram using kernel density estimation with a gaussian kernel (on a non uniformly sampled distribution)
UniformSample<double> kernelDensityEstimation(const NonUniformHistogram& histogram, double h=nan, bool normalize=false) {
    const double N = histogram.sampleCount();
    if(h==0 || isNaN(h)) h = pow(4./(3*N),1./5) * sqrt(histogram.variance());
    float max = ::max(histogram.keys);
    float delta=__FLT_MAX__;
    for(uint i: range(histogram.keys.size-1)) {
        float diff = histogram.keys[i+1]-histogram.keys[i];
        assert_(diff>0, histogram.keys[i], histogram.keys[i+1]);
        delta=min(delta, diff);
    }
    uint sampleCount = max/delta;
    UniformSample<double> pdf ( sampleCount );
    pdf.scale = delta;
    const float scale = 1./h/*Normalize kernel (area=1)*/ * (normalize ? 1./N : 1)/*Normalize sampleCount (to density)*/;
    parallel(pdf.size, [&](uint, uint i) {
        const float x0 = i*delta;
        float sum = 0;
        for(auto sample: histogram) sum += sample.value * exp(-1./2*sq((x0-sample.key)/h))/sqrt(2*PI);
        pdf[i] = scale * sum;
    });
    return pdf;
}

class(KernelDensityEstimation, Operation), virtual Pass {
    virtual string parameters() const { return "bandwidth normalize"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        target.metadata = String("kde.tsv"_);
        NonUniformHistogram H = parseNonUniformSample<double,int64>(source.data);
        for(uint i: range(H.size())) if(H.keys[i] != i) target.data = toASCII(kernelDensityEstimation(H, toDecimal(args.value("bandwidth"_)), args.value("normalize"_,"0"_)!="0"_)); // Non uniform KDE
        else toASCII(kernelDensityEstimation(copy(H.values)));
    }
};

//definePass(Sum, "scalar"_, str(sum(parseUniformSample(source.data))) );
class(Sum, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = String("scalar"_);
        target.data = str(parseUniformSample<double>(source.data).sum())+"\n"_;
    }
};

template<Type X, Type Y> NonUniformSample<X,Y> squareRootVariable(NonUniformSample<X,Y>&& A) { for(X& x: A.keys) x=sqrt(x); return move(A); }
/// Square roots the variable of a distribution
class(SquareRootVariable, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_), "Expected a distribution, not a", source.metadata, source.name, target.name);
        target.metadata = copy(source.metadata);
        target.data = toASCII(squareRootVariable(parseNonUniformSample<double,double>(source.data)));
    }
};

/// Scales variable
template<Type X, Type Y> NonUniformSample<X,Y> scaleVariable(float scalar, NonUniformSample<X,Y>&& A) { for(X& x: A.keys) x *= scalar; return move(A); }
/// Scales the variable of a distribution
class(ScaleVariable, Operation), virtual Pass {
    virtual string parameters() const { return "scale"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_), "Expected a distribution, not a", source.metadata, source.name, target.name);
        target.metadata = copy(source.metadata);
        target.data = toASCII(scaleVariable(toDecimal(args.at("scale"_)), parseNonUniformSample<double,double>(source.data)));
    }
};

/// Divides two scalars
class(Div, Operation) {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        outputs[0]->metadata = copy(inputs[0]->metadata);
        if(inputs[0]->metadata=="scalar"_) outputs[0]->data = ftoa(TextData(inputs[0]->data).decimal()/TextData(inputs[1]->data).decimal(), 4)+"\n"_;
        else if(endsWith(inputs[0]->metadata,".tsv"_)) outputs[0]->data = toASCII( (1./TextData(inputs[1]->data).decimal()) * parseNonUniformSample<double,double>(inputs[0]->data) );
    }
};
