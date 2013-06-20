/// \file analysis.cc Statistical analysis operations
#include "sample.h"
#include "operation.h"
#include "math.h"

/// Computes relative errors (assuming last value is correct)
class(RelativeError, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_));
        assert_(source.data, source.elements);
        NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(source.data);
        buffer<double> relativeError ( sample.size() );
        const auto& f = sample.values;
        for(uint i: range(f.size)) relativeError[i] = abs((f[i]/f.last())-1);
        target.metadata = copy(source.metadata);
        target.data = toASCII( NonUniformSample<double,double>(sample.keys, relativeError) );
    }
};

/// Computes running average (i.e convolution with a box kernel)
class(RunningAverage, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_));
        NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(source.data);
        const int window = 1; // Half window size
        buffer<double> average ( sample.size()-2*window );
        const auto& f = sample.values;
        for(uint i: range(window,f.size-window)) {
            double sum = 0; for(uint j: range(i-window,i+window+1)) sum += f[j];
            const uint sampleCount = window+1+window;
            average[i-window] = sum/sampleCount;
        }
        target.metadata = copy(source.metadata);
        target.data = toASCII( NonUniformSample<double,double>(sample.keys.slice(window,average.size), average) );
    }
};

class(Optimize, Operation), virtual Pass {
    string parameters() const override { return "criteria constraint"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        string criteria = args.at("criteria"_);
        TextData s(args.at("constraint"_));
        string op = s.whileAny("><"_);
        double value = s.decimal();
        const NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(source.data);
        target.metadata = String("scalar"_);
        assert_(!target.data);
        double best = nan;
        for(const_pair<double,double> s: sample) {
            bool good=false; // Inside constraints
            /***/ if(op=="<"_) good=s.value<value;
            else if(op==">"_) good=s.value>value;
            else error("Unknown constraint operator", op);
            if(!good) continue;
            bool better=false; // Better than current best
            double criteriaValue = nan;
            /***/ if(criteria=="minimum"_) { better=s.value<best; criteriaValue=s.value; }
            else if(criteria=="maximum"_) { better=s.value>best; criteriaValue=s.value; }
            else if(criteria=="minimumArgument"_) { better=s.key<best; criteriaValue=s.key; }
            else if(criteria=="maximumArgument"_) { better=s.key>best; criteriaValue=s.key; }
            else error("Unknown criteria", criteria);
            if(better || !target.data) best = criteriaValue;
            target.data = toASCII(best);
        }
        // Returns empty data if no values respect contraints
    }
};
