/// \file analysis.cc Statistical analysis operations
#include "sample.h"
#include "operation.h"
#include "math.h"

/// Computes sequence of relative deviation between neighbouring values
class(RunningDeviation, Operation), virtual Pass {
    virtual void execute(const Dict& , Result& target, const Result& source) override {
        NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(source.data);
        const int window = 1; // Half window size
        buffer<double> relativeDeviations ( sample.size()-2*window );
        const auto& f = sample.values;
        for(uint i: range(window,f.size-window)) {
            double sum = 0;
            for(uint j: range(i-window,i+window+1)) sum += f[j];
            const uint sampleCount = window+1+window;
            const double mean = sum/sampleCount;
            double sumOfSquaredDifferences = 0;
            for(uint j: range(i-window,i+window+1)) sumOfSquaredDifferences += sq(f[j]-mean);
            const double variance = sumOfSquaredDifferences/sampleCount;
            const double deviation = sqrt(variance);
            const double relativeDeviation = deviation/mean;
            relativeDeviations[i-window] = relativeDeviation;
        }
        target.metadata = copy(source.metadata);
        target.data = toASCII(NonUniformSample<double,double>(sample.keys.slice(window,relativeDeviations.size),relativeDeviations));
    }
};

class(Optimize, Operation), virtual Pass {
    string parameters() const override { return "criteria constraint"_; }
    virtual void execute(const Dict& args, Result& target, const Result& source) override {
        string criteria = args.at("criteria"_);
        TextData s(args.at("constraint"_));
        string op = s.whileAny("><"_);
        double value = s.decimal();
        NonUniformSample<double,double> sample = parseNonUniformSample<double,double>(source.data);
        target.metadata = String("scalar"_);
        double best=nan;
        for(const pair<double,double>& s: sample) {
            bool good=false; // Inside constraints
            /***/ if(op=="<"_) good=s.value<value;
            else if(op==">"_) good=s.value>value;
            else error("Unknown constraint operator", op);
            if(!good) continue;
            bool better=false; // Better than current best
            double criteriaValue;
            /***/ if(criteria=="minimum"_) better=s.value<best, criteriaValue=s.value;
            else if(criteria=="maximum"_) better=s.value>best, criteriaValue=s.value;
            else if(criteria=="minimumArgument"_) better=s.key<best, criteriaValue=s.key;
            else if(criteria=="maximumArgument"_) better=s.key>best, criteriaValue=s.key;
            else error("Unknown criteria", criteria);
            if(better || isNaN(best)) best = criteriaValue;
        }
        target.data = isNaN(best) ? String() : toASCII(best);
        // Returns empty data if no values respect contraints
    }
};
