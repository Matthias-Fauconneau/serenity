/// \file analysis.cc Statistical analysis operations
#include "sample.h"
#include "operation.h"
#include "math.h"

/// Computes relative errors (assuming last value is correct)
class(RelativeError, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_));
        if(source.data) { // Scalar
            assert_(!source.elements);
            NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(source.data);
            double correct = sample.values.last();
            buffer<double> relativeError ( sample.size()-1 );
            for(uint i: range(sample.values.size)) relativeError[i] = abs((sample.values[i]/correct)-1);
            target.metadata = copy(source.metadata);
            target.data = toASCII( NonUniformSample<double,double>(sample.keys.slice(0, sample.size()-1), relativeError) );
        }
        if(source.elements) { // Sample
            assert_(!source.data && source.elements.size()>1);
            string reference = source.elements.keys.last();
            NonUniformSample<double,double> correct = parseNonUniformSample<double,double>(source.elements.values.last());
            NonUniformSample<double,double> errors;
            for(auto element: source.elements) {
                if(element.key == reference) continue;
                NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(element.value);
                assert_(sample.size()<=correct.size(), sample.size(), correct.size(), source.elements.keys);
                UniformSample<double> error ( correct.size() );
                const auto& f = sample.values;
                for(uint i: range(error.size)) error[i] = abs((i<f.size?f[i]:0)-correct.values[i]);
                errors.insert(toDecimal(element.key), error.sum() / correct.sum() );
            }
            target.data = toASCII( errors );
        }
        target.metadata = copy(source.metadata);
    }
};

/*class(Error, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_));
        if(source.elements) { // Sample
            assert_(!source.data && source.elements.size()>1);
            string reference = source.elements.keys.last();
            NonUniformSample<double,double> correct = parseNonUniformSample<double,double>(source.elements.values.last());
            for(auto element: source.elements) {
                if(element.key == reference) continue;
                NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(element.value);
                assert_(sample.size()<=correct.size(), sample.size(), correct.size(), source.elements.keys);
                buffer<double> error ( correct.size() );
                const auto& f = sample.values;
                for(uint i: range(error.size)) error[i] = abs((i<f.size?f[i]:0)-correct.values[i]);
                target.elements.insert(copy(element.key), toASCII( NonUniformSample<double,double>(correct.keys, error) ));
            }
        }
        target.metadata = copy(source.metadata);
    }
};*/

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
