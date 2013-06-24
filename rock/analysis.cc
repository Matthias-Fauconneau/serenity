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
    string parameters() const override { return "reference slice"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        assert_(endsWith(source.metadata,".tsv"_));
        if(source.elements) { // Sample
            assert_(!source.data && source.elements.size()>1);
            array<NonUniformSample> samples; for(auto element: source.elements) samples << parseNonUniformSample(element.value);
            double delta = __DBL_MAX__, maximum=0;
            for(const NonUniformSample& sample: samples) delta=::min(delta, sample.delta()), maximum=max(maximum, max(sample.keys));
            uint sampleCount = maximum/delta;

            NonUniformSample reference;
            if(args.at("reference"_)=="last"_) reference = samples.pop();
            else if(args.at("reference"_)=="mean"_) {
                for(uint i: range(0, sampleCount)) { // Computes mean distribution (uniform sampliong, 0th order interpolation)
                    double x = i*delta;
                    double sum=0;
                    for(const NonUniformSample& sample: samples) sum += sample.interpolate(x);
                    double mean = sum/samples.size;
                    reference.insert(x, mean);
                }
            } else error("Unknown reference", args.at("reference"_));

            string slice = args.value("slice"_,"0-1"_);
            assert_(slice.contains('-'));
            double sliceMin = toDecimal(section(slice,'-',0,1)), sliceMax = toDecimal(section(slice,'-',1,2));

            ScalarMap relativeError;
            for(uint i: range(samples.size)) {
                const NonUniformSample& sample = samples[i];

                double differenceArea = 0, referenceArea = 0;
                for(uint i: range(round(sliceMin*sampleCount), round(sliceMax*sampleCount))) { // Computes area between curves (with 0th order numerical integration)
                    double x = i*delta;
                    differenceArea += abs(sample.interpolate(x)-reference.interpolate(x));
                    referenceArea += reference.interpolate(x);
                }
                assert_(differenceArea && referenceArea);
                relativeError.insert(copy(source.elements.keys[i]), differenceArea / referenceArea );
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
