/// \file sample.h Methods for (non-)uniformly sampled distribution
#pragma once
#include "string.h"
#include "map.h"
#include "data.h"

// UniformSample

/// Uniformly sampled distribution
generic struct UniformSample : buffer<T> {
    using buffer<T>::buffer;
    UniformSample(buffer<T>&& A):buffer<T>(move(A)){}
    /// Returns the sum of the samples
    T sum() const { T sum=0; for(uint i: range(size)) sum += at(i); return sum; }
    /// Returns the mean of the samples
    T mean() const { return sum()/size; }
    /// Returns the variance of the samples
    T variance() const { float mean=UniformSample::mean(), ssd=0; for(uint i: range(size)) ssd += sq(at(i)-mean); return ssd/size; }

    using buffer<T>::at;
    using buffer<T>::size;
    float scale=1; // Scales from integer sample indices to floating-point sample positions
};
/// Multiplies sample by a scalar
generic UniformSample<T> operator*(T scalar, const UniformSample<T>& A) { uint N=A.size; UniformSample<T> R(N); for(uint i: range(N)) R[i]=scalar*A[i]; return R; }
/// Parses a uniformly sampled distribution from tab-separated values
generic UniformSample<T> parseUniformSample(const string& file) {
    TextData s (file);
    int maximum=0; while(s) { maximum=max(maximum, int(s.decimal())); s.skip("\t"_); s.decimal(); s.skip("\n"_); }
    s.index=0;
    UniformSample<T> sample(maximum+1);
    while(s) { double x=s.decimal(); assert_(x==double(int(x))); s.skip("\t"_); sample[int(x)]=s.decimal(); s.skip("\n"_); }
    return sample;
}
/// Converts a uniformly sampled distribution to tab-separated values
generic String toASCII(const UniformSample<T>& A) {
    String s;
    for(uint i=0; i<A.size; i++) s << ftoa(i,4,0,true) << '\t' << ftoa(A[i],4,0,true) << '\n';
    return s;
}

/// Represents a sample distribution by its histogram
struct UniformHistogram : UniformSample<uint64> {
    using UniformSample::UniformSample;
    UniformHistogram(UniformSample&& A):UniformSample(move(A)){}
    /// Returns the number of samples represented by this histogram
    uint64 sampleCount() const { return UniformSample::sum(); }
    /// Returns the sum of the samples represented by the histogram
    double sum() const { double sum=0; for(uint i: range(size)) sum += i*at(i); return sum; }
    /// Returns the mean of the samples represented by the histogram
    double mean() const { return sum()/sampleCount(); }
    /// Returns the variance of the samples represented by the histogram
    double variance() const { float sampleMean=mean(), ssd=0; for(uint i: range(size)) ssd += at(i)*sq(i-sampleMean); return ssd/sampleCount(); }
};

// NonUniformSample

/// Non-uniformly sampled distribution
template<Type X, Type Y> struct NonUniformSample : map<X, Y> {
    /// Returns the sum of the samples
    Y sum() const { Y sum=0; for(uint i: range(values.size)) sum += values[i]; return sum; }

    using map<X, Y>::values;
};
/// Multiplies sample by a scalar
template<Type X, Type Y> NonUniformSample<X,Y> operator*(float scalar, NonUniformSample<X,Y>&& A) { for(double& x: A.values) x *= scalar; return move(A); }
/// Parses a uniformly sampled distribution from tab-separated values
template<Type X, Type Y> NonUniformSample<X,Y> parseNonUniformSample(const string& file) {
    TextData s (file);
    NonUniformSample<X,Y> sample;
    while(s) { double x=s.decimal(); s.skip("\t"_); sample.insert(x, s.decimal()); s.skip("\n"_); }
    return sample;
}
/// Converts a uniformly sampled distribution to tab-separated values
template<Type X, Type Y> String toASCII(const NonUniformSample<X,Y>& A) {
    String s;
    for(auto sample: A) s << ftoa(sample.key, 4) << '\t' << ftoa(sample.value, 4, 0, true) << '\n';
    return s;
}

struct NonUniformHistogram : NonUniformSample<double, uint64> {
    using NonUniformSample::NonUniformSample;
    NonUniformHistogram(NonUniformSample&& A):NonUniformSample(move(A)){}
    /// Returns the number of samples represented by this histogram
    uint64 sampleCount() const { return NonUniformSample::sum(); }
    /// Returns the sum of the samples represented by the histogram
    double sum() const { double sum=0; for(auto sample: *this) sum += sample.value*sample.key; return sum; }
    /// Returns the mean of the samples represented by the histogram
    double mean() const { return sum()/NonUniformSample::sum(); }
    /// Returns the variance of the samples represented by the histogram
    double variance() const { double sampleMean=mean(), ssd=0; for(auto sample: *this) ssd += sample.value*sq(sample.key-sampleMean); return ssd/sampleCount(); }
};
