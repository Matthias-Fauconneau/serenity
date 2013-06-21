/// \file analysis.cc Statistical analysis operations
#include "sample.h"
#include "operation.h"
#include "math.h"

/// Computes relative errors (assuming last value is correct)
class(ScalarError, Operation), virtual Pass {
    virtual void execute(const Dict&, Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_));
        if(source.data) { // Scalar
            assert_(!source.elements);
            NonUniformSample sample = parseNonUniformSample(source.data);
            double correct = sample.values.last();
            buffer<double> relativeError ( sample.size()-1 );
            for(uint i: range(relativeError.size)) relativeError[i] = abs((sample.values[i]/correct)-1);
            target.metadata = copy(source.metadata);
            target.data = toASCII( NonUniformSample(sample.keys.slice(0, relativeError.size), relativeError) );
        }
        target.metadata = copy(source.metadata);
    }
};

class(DistributionError, Operation), virtual Pass {
    string parameters() const override { return "slice"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_));
        if(source.elements) { // Sample
            assert_(!source.data && source.elements.size()>1);
            string reference = source.elements.keys.last();
            NonUniformSample correct = parseNonUniformSample(source.elements.values.last());
            NonUniformSample relativeError;
            for(auto element: source.elements) {
                if(element.key == reference) continue;
                NonUniformSample sample = parseNonUniformSample(element.value);
                double delta = min(correct.delta(), sample.delta()), maximum=max(max(correct.keys), max(sample.keys));
                uint sampleCount = maximum/delta;
                string slice = args.value("slice"_,"0-1"_);
                assert_(slice.contains('-'));
                double sliceMin = toDecimal(section(slice,'-',0,1)), sliceMax = toDecimal(section(slice,'-',1,2));
                double differenceArea = 0, correctArea = 0;
                for(uint i: range(round(sliceMin*sampleCount), round(sliceMax*sampleCount))) { // Computes area between curves (with 0th order numerical integration)
                    double x = i*delta;
                    differenceArea += abs(sample.interpolate(x)-correct.interpolate(x));
                    correctArea += correct.interpolate(x);
                }
                assert_(differenceArea && correctArea);
                relativeError.insert(toDecimal(element.key), differenceArea / correctArea );
            }
            target.data = toASCII( relativeError );
        }
        target.metadata = copy(source.metadata);
    }
};

class(Optimize, Operation), virtual Pass {
    string parameters() const override { return "criteria constraint"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        string criteria = args.at("criteria"_);
        TextData s(args.at("constraint"_));
        string op = s.whileAny("><"_);
        double value = s.decimal();
        const NonUniformSample sample = parseNonUniformSample(source.data);
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
