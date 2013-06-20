/// \file sample.h Methods for (non-)uniformly sampled distribution
#pragma once
#include "string.h"
#include "map.h"
#include "data.h"
#include "math.h"

// Scalar
inline String toASCII(double scalar) { return ftoa(scalar, 5)+"\n"_; }

// Vector
generic struct Vector : buffer<T> {
       using buffer<T>::buffer;
       Vector(buffer<T>&& A):buffer<T>(move(A)){}
};
/// Multiplies vector by a scalar
generic Vector<T> operator*(T scalar, const Vector<T>& A) { uint N=A.size; Vector<T> R(N); for(uint i: range(N)) R[i]=scalar*A[i]; return R; }
inline Vector<int> round(const Vector<double>& A) { uint N=A.size; Vector<int> R(N); for(uint i: range(N)) R[i]=round(A[i]); return R; }
/// Parses a vector from comma-separated values
generic Vector<T> parseVector(const string& file)  {
    TextData s (file);
    Vector<T> vector(count(file,',')+1);
    for(uint i: range(vector.size)) { vector[i]=s.decimal(); if(i<vector.size-1) s.skip(","_); s.skip(); }
    return vector;
}
template<> inline Vector<int> parseVector(const string& file)  {
    TextData s (file);
    Vector<int> vector(count(file,',')+1);
    for(uint i: range(vector.size)) { vector[i]=s.integer(); if(i<vector.size-1) s.skip(","_); s.skip(); }
    return vector;
}
generic String toASCII(const Vector<T>& a) { String s; for(uint i: range(a.size)) { s<<str(a[i]); if(i<a.size-1) s<<", "_;} s<<"\n"_; return s; }

// UniformSample

/// Uniformly sampled distribution
generic struct UniformSample : Vector<T> {
    using Vector<T>::Vector;
    UniformSample(Vector<T>&& A):Vector<T>(move(A)){}
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
generic UniformSample<T> operator*(T scalar, const UniformSample<T>& A) { return scalar*(const Vector<T>&)A; }
/// Parses a uniformly sampled distribution from tab-separated values
generic UniformSample<T> parseUniformSample(const string& file) {
    TextData s (file);
    UniformSample<T> sample(count(file,'\n'));
    for(uint i: range(sample.size)) { double x=s.decimal(); assert_(x==i); s.skip("\t"_); sample[i]=s.decimal(); s.skip("\n"_); }
    return sample;
}
/// Converts a uniformly sampled distribution to tab-separated values
generic String toASCII(const UniformSample<T>& A) {
    String s;
    for(uint i=0; i<A.size; i++) s << ftoa(A.scale*i,4,0,true) << '\t' << ftoa(A[i],4,0,true) << '\n';
    return s;
}

/// Represents a sample distribution by its histogram
struct UniformHistogram : UniformSample<int64> {
    using UniformSample::UniformSample;
    UniformHistogram(UniformSample&& A):UniformSample(move(A)){}
    /// Returns the number of samples represented by this histogram
    uint64 sampleCount() const { return UniformSample::sum(); }
    /// Returns the sum of the samples represented by the histogram
    double sum() const { double sum=0; for(uint i: range(size)) sum += i*at(i); return sum; }
    /// Returns the mean of the samples represented by the histogram
    double mean() const { return sum()/sampleCount(); }
    /// Returns the variance of the samples represented by the histogram
    double variance() const { float sampleMean=mean(), ssd=0; for(uint i: range(size)) ssd += at(i)*sq(i-sampleMean); return ssd/(sampleCount()-1); }
};

// NonUniformSample

/// Non-uniformly sampled distribution
template<Type X, Type Y> struct NonUniformSample : map<X, Y> {
    using map<X, Y>::map;
    //NonUniformSample(map<X, Y>&& A):map<X, Y>(move(A)){}

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

struct NonUniformHistogram : NonUniformSample<double, int64> {
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

/// Set of named scalars
//generic using ScalarMap = map<String, T>;
typedef map<String, double> ScalarMap;

//generic ScalarMap<T> parseMap(const string& file) {
inline ScalarMap parseMap(const string& file) {
    map<String, double> dict;
    for(TextData s(file);s;) {
        string key = s.until('\t');
        string value = s.until('\n');
        dict.insert(String(key), toDecimal(value));
    }
    return dict;
}
