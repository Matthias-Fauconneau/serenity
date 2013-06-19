/// \file analysis.cc Statistical analysis operations
#include "sample.h"
#include "operation.h"

/// Computes sequence of relative errors between subsequent values
class(RunningDeviation, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(source.data);
        buffer<double> deviation ( sample.size()-1 );
        const auto& f = sample.values;
        for(uint i: range(deviation.size)) deviation[i] = abs(f[i] - f[i+1]) / min(f[i], f[i+1]);
        target.metadata = copy(source.metadata);
        target.data = toASCII(NonUniformSample<double,double>(sample.keys.slice(0,deviation.size),deviation));
    }
};

class(MinimumArgument, Operation), virtual Pass {
    string parameters() const override { return "constraint"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        TextData s(args.at("constraint"_)); s.skip("<"_); double value = s.decimal();
        NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(source.data);
        target.metadata = String("scalar"_);
        for(auto s: sample) if(s.value<value) { target.data = toASCII(s.key); return; }
    }
};
