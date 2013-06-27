/// \file sample.cc Methods for (non-)uniformly sampled distribution
#include "sample.h"
#include "data.h"

// Scalar
real parseScalar(const string& data) { return TextData(data).decimal(); }
String toASCII(real scalar) { return ftoa(scalar, 5)+"\n"_; }

// Vector
typedef buffer<real> Vector;
real sum(const Vector& A) { real sum=0; for(uint i: range(A.size)) sum += A[i]; return sum; }
Vector operator*(real scalar, const Vector& A) { uint N=A.size; Vector R(N); for(uint i: range(N)) R[i]=scalar*A[i]; return R; }
Vector round(const Vector& A) { uint N=A.size; Vector R(N); for(uint i: range(N)) R[i]=round(A[i]); return R; }
Vector abs(const Vector& A) { uint N=A.size; Vector R(N); for(uint i: range(N)) R[i]=abs(A[i]); return R; }
Vector sq(const Vector& A) { uint N=A.size; Vector R(N); for(uint i: range(N)) R[i]=sq(A[i]); return R; }
Vector operator+(const Vector& A, const Vector& B) { uint N=A.size; Vector R(N); for(uint i: range(N)) R[i]=A[i]+B[i]; return R; }
Vector operator-(const Vector& A, const Vector& B) { uint N=A.size; Vector R(N); for(uint i: range(N)) R[i]=A[i]-B[i]; return R; }
Vector parseVector(const string& file, bool integer)  {
    TextData s (file);
    Vector vector(count(file,',')+1);
    for(uint i: range(vector.size)) { vector[i] = integer ? s.integer() : s.decimal(); if(i<vector.size-1) s.skip(","_); s.skip(); }
    return vector;
}
String toASCII(const Vector& a) { String s; for(uint i: range(a.size)) { s<<str(a[i]); if(i<a.size-1) s<<", "_;} s<<"\n"_; return s; }

// UniformSample
real UniformSample::mean() const { return sum()/size; }
real UniformSample::variance() const { real mean=UniformSample::mean(), ssd=0; for(uint i: range(size)) ssd += sq(at(i)-mean); return ssd/(size-1); }
UniformSample parseUniformSample(const string& file) {
    TextData s (file);
    UniformSample sample(count(file,'\n'));
    for(uint i: range(sample.size)) { real x=s.decimal(); assert_(x==i); s.skip("\t"_); sample[i]=s.decimal(); s.skip("\n"_); }
    return sample;
}
String toASCII(const UniformSample& A) {
    String s;
    for(uint i=0; i<A.size; i++) s << ftoa(A.scale*i,4,0,true) << '\t' << ftoa(A[i],4,0,true) << '\n';
    return s;
}
/// Returns a slice (by reference) a range of values from a uniformly sampled distribution (discards original variable)
UniformSample slice(const UniformSample& sample, real sliceBegin, real sliceEnd) {
    int begin = round(sliceBegin*sample.size), end = round(sliceEnd*sample.size);
    return UniformSample(sample.slice(begin,end-begin));
}

// UniformSample[]/// Estimates mean distribution from distribution samples
UniformSample mean(const ref<UniformSample>& samples) {
    UniformSample mean (samples[0].size);
    for(uint i: range(mean.size)) {
        real sum = 0;
        for(const UniformSample& sample: samples) sum += sample[i];
        mean[i] = sum/samples.size;
    }
    return mean;
}

// UniformHistogram
real UniformHistogram::sum() const { real sum=0; for(uint i: range(size)) sum += i*at(i); return sum; }
real UniformHistogram::variance() const { real sampleMean=mean(), ssd=0; for(uint i: range(size)) ssd += at(i)*sq(i-sampleMean); return ssd/(sampleCount()-1); }

// NonUniformSample
real NonUniformSample::sum() const { real sum=0; for(uint i: range(values.size)) sum += values[i]; return sum; }
real NonUniformSample::delta() const {
    real delta=__DBL_MAX__;
    for(uint i: range(keys.size-1)) {
        real diff = keys[i+1]-keys[i];
        assert_(diff>0, keys[i], keys[i+1]);
        delta=min(delta, diff);
    }
    return delta;
}
real NonUniformSample::interpolate(real x) const {
    real nearest=__DBL_MAX__, value=nan;
    for(auto e: *this) { real d=abs(e.key-x); if(d < nearest) nearest=d, value=e.value; }
    return value;
}
NonUniformSample operator*(real scalar, NonUniformSample&& A) { for(real& x: A.values) x *= scalar; return move(A); }
array<UniformSample> resample(const ref<NonUniformSample>& nonUniformSamples) {
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
    return samples;
}
NonUniformSample parseNonUniformSample(const string& file) {
    TextData s (file);
    NonUniformSample sample;
    while(s) { if(s.match('#')) s.until('\n'); else { real x=s.decimal(); s.skip("\t"_); sample.insert(x, s.decimal()); s.skip("\n"_); } }
    return sample;
}
String toASCII(const NonUniformSample& A) {
    String s;
    for(auto sample: A) s << ftoa(sample.key, 4) << '\t' << ftoa(sample.value, 4, 0, true) << '\n';
    return s;
}

//NonUniformHistogram
real NonUniformHistogram::sum() const { real sum=0; for(auto sample: *this) sum += sample.value*sample.key; return sum; }
real NonUniformHistogram::variance() const { real sampleMean=mean(), ssd=0; for(auto sample: *this) ssd += sample.value*sq(sample.key-sampleMean); return ssd/sampleCount(); }

// ScalarMap
ScalarMap parseMap(const string& file) {
    map<String, real> dict;
    for(TextData s(file);s;) {
        string key = s.until('\t');
        string value = s.until('\n');
        dict.insert(String(key), toDecimal(value));
    }
    return dict;
}
String toASCII(const ScalarMap& A) {
    String s;
    for(auto sample: A) s << sample.key << '\t' << ftoa(sample.value, 4, 0, true) << '\n';
    return s;
}
