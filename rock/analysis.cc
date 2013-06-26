/// \file analysis.cc Statistical analysis operations
#include "sample.h"

/// Computes deviation of a sample of distributions
/// \note This methods assumes samples are uncorrelated
real relativeDeviation(const ref<NonUniformSample>& nonUniformSamples, string slice) {
    real delta = __DBL_MAX__, maximum=0;
    for(const NonUniformSample& sample: nonUniformSamples) delta=::min(delta, sample.delta()), maximum=max(maximum, max(sample.keys));
    uint sampleCount = maximum/delta;
    /// Uniformly resamples all distributions
    array<UniformSample> samples;
    for(const NonUniformSample& sample: nonUniformSamples) {
        UniformSample uniform ( sampleCount );
        for(uint i: range(0, sampleCount)) uniform[i] = sample.interpolate( delta*i );
        samples << move(uniform);
    }
    UniformSample mean ( sampleCount );
    for(uint i: range(0, sampleCount)) {
        real sum = 0;
        for(const UniformSample& sample: samples) sum += sample[i];
        mean[i] = sum/samples.size;
    }

    assert_(slice.contains('-'));
    real sliceMin = toDecimal(section(slice,'-',0,1)), sliceMax = toDecimal(section(slice,'-',1,2));

    real meanNorm = 0, sumOfSquareDifferences = 0;
    for(uint i: range(round(sliceMin*sampleCount), round(sliceMax*sampleCount))) {
        meanNorm += mean[i]; // Computes mean sample count
        for(const UniformSample& sample: samples) sumOfSquareDifferences += sq(sample[i]-mean[i]);
    }
    real unbiasedVarianceEstimator = sumOfSquareDifferences / (samples.size-1);
    return sqrt(unbiasedVarianceEstimator) / meanNorm;
}

#if 0
real optimize(string criteria, string constraint) {
        TextData s(constraint);
        string op = s.whileAny("><"_);
        real value = s.decimal();
        const NonUniformSample sample = parseNonUniformSample(source.data);
        target.metadata = String("scalar"_);
        assert_(!target.data);
        real best = nan;
        for(const_pair<real,real> s: sample) {
            bool good=false; // Inside constraints
            /***/ if(op=="<"_) good=s.value<value;
            else if(op==">"_) good=s.value>value;
            else error("Unknown constraint operator", op);
            if(!good) continue;
            bool better=false; // Better than current best
            real criteriaValue = nan;
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
#endif
