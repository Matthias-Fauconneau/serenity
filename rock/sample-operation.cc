/// \file sample-operation.cc wraps Sample methods as Operation
#include "sample.h"
#include "operation.h"

/// Slices sample
struct Slice : Pass {
    virtual string parameters() const { return "begin"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        target.data = toASCII(UniformSample(parseUniformSample(source.data).slice(fromInteger(args.at("begin"_)))));
    }
};
template struct Interface<Operation>::Factory<Slice>;

struct Sum : Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = String("scalar"_);
        target.data = toASCII( parseNonUniformSample(source.data).sum() );
    }
};
template struct Interface<Operation>::Factory<Sum>;

NonUniformSample squareRootVariable(NonUniformSample&& A) { for(real& x: A.keys) x=sqrt(x); return move(A); }
/// Square roots the variable of a distribution
struct SquareRootVariable : Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,"tsv"_), "Expected a distribution, not a", source.metadata, source.name, target.name);
        target.metadata = copy(source.metadata);
        target.data = toASCII(squareRootVariable(parseNonUniformSample(source.data)));
    }
};
template struct Interface<Operation>::Factory<SquareRootVariable>;

/// Scales variable
NonUniformSample scaleVariable(float scalar, NonUniformSample&& A) { for(real& x: A.keys) x *= scalar; return move(A); }
/// Scales the variable of a distribution
struct ScaleVariable : Operation {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        assert_(endsWith(inputs[0]->metadata,"tsv"_), "Expected a distribution, not a", "'"_+inputs[0]->metadata+"'"_, "for input", "'"_+inputs[0]->name+"'"_);
        assert_(inputs[1]->metadata=="scalar"_ || inputs[1]->metadata=="argument"_, "Expected a scalar or an argument, not a", inputs[1]->metadata,"for", inputs[1]->name);
        outputs[0]->metadata = copy(inputs[0]->metadata);
        outputs[0]->data = toASCII(scaleVariable(parseScalar(inputs[1]->data), parseNonUniformSample(inputs[0]->data)));
    }
};
template struct Interface<Operation>::Factory<ScaleVariable>;

/// Scales both the variable and the values of a distribution to keep the same area
struct ScaleDistribution : Operation {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        assert_(endsWith(inputs[0]->metadata,"tsv"_), "Expected a distribution, not a", "'"_+inputs[0]->metadata+"'"_, "for input", "'"_+inputs[0]->name+"'"_);
        assert_(inputs[1]->metadata=="scalar"_ || inputs[1]->metadata=="argument"_, "Expected a scalar or an argument, not a", inputs[1]->metadata,"for", inputs[1]->name);
        string xlabel,ylabel; { TextData s(inputs[0]->metadata); ylabel = s.until('('); xlabel = s.until(')'); }
        outputs[0]->metadata = ylabel+"("_+xlabel+" [μm]).tsv"_; //FIXME: get unit from scalar input
        outputs[0]->data = toASCII(scaleDistribution(parseScalar(inputs[1]->data), parseNonUniformSample(inputs[0]->data)));
    }
};
template struct Interface<Operation>::Factory<ScaleDistribution>;

/// Divides two scalars / vectors / sample
struct Div : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        outputs[0]->metadata = copy(inputs[0]->metadata);
        if(inputs[0]->metadata=="scalar"_) outputs[0]->data = toASCII(parseScalar(inputs[0]->data)/parseScalar(inputs[1]->data));
        else if(inputs[0]->metadata=="vector"_) outputs[0]->data = toASCII( (1./parseScalar(inputs[1]->data)) * parseVector(inputs[0]->data) );
        else if(endsWith(inputs[0]->metadata,"tsv"_)) outputs[0]->data = toASCII( (1./parseScalar(inputs[1]->data)) * parseNonUniformSample(inputs[0]->data) );
        else error(inputs[0]->metadata);
    }
};
template struct Interface<Operation>::Factory<Div>;

struct NormalizeX : Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        UniformSample sample = parseUniformSample(source.data);
        sample.scale = 1./(sample.size-1); // Normalizes X axis
    }
};
template struct Interface<Operation>::Factory<NormalizeX>;

struct NormalizeAreaY : Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        UniformSample sample = parseUniformSample(source.data);
        float sum = sample.sum();
        assert_(sum);
        target.data = toASCII((1./(sample.scale*sum))*sample); // Normalize Y axis by integral
    }
};
template struct Interface<Operation>::Factory<NormalizeAreaY>;

struct NormalizeAreaXY : Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = copy(source.metadata);
        UniformSample sample = parseUniformSample(source.data);
        sample.scale = 1./(sample.size-1); // Also normalizes X axis
        float sum = sample.sum();
        assert_(sum);
        target.data = toASCII((1./(sample.scale*sum))*sample); // Normalize Y axis by integral
    }
};
template struct Interface<Operation>::Factory<NormalizeAreaXY>;

/// Computes the mean of the values sampled by the histogram
struct HistogramMean : Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = String("scalar"_);
        NonUniformHistogram histogram = parseNonUniformSample(source.data);
        real mean = histogram.mean();
        assert_(mean);
        target.data = toASCII(mean);
    }
};
template struct Interface<Operation>::Factory<HistogramMean>;

/// Computes the median of the values sampled by the histogram
struct HistogramMedian : Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        target.metadata = String("scalar"_);
        NonUniformHistogram histogram = parseNonUniformSample(source.data);
        uint64 sampleCount = histogram.sampleCount();
        uint64 sum = 0; real median = 0;
        for(pair<real,real> value_count: histogram) {
            real value = value_count.key, count = value_count.value;
            sum += count;
            if(sum >= sampleCount) { median = value; break; } // FIXME: bias, TODO: linear interpolation
        }
        assert_(median);
        target.data = toASCII(median);
    }
};
template struct Interface<Operation>::Factory<HistogramMedian>;
