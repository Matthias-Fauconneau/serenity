/// \file sample.h Methods for (non-)uniformly sampled distribution
#pragma once
#include "string.h"
#include "map.h"
#include "math.h"

// Scalar
real parseScalar(const string& data);
String toASCII(real scalar);

// Vector
typedef buffer<real> Vector;
/// Returns the sum of the coordinates
real sum(const Vector& A);
/// Multiplies vector by a scalar
Vector operator*(real scalar, const Vector& A);
/// Rounds to integer
Vector round(const Vector& A);
/// Computes absolute values
Vector abs(const Vector& A);
/// Computes square values
Vector sq(const Vector& A);
/// Adds two vectors
Vector operator+(const Vector& A, const Vector& B);
/// Substracts two vectors
Vector operator-(const Vector& A, const Vector& B);

/// Parses a vector from comma-separated values
Vector parseVector(const string& file, bool integer=false);
String toASCII(const Vector& a);

// UniformSample

/// Uniformly sampled distribution
struct UniformSample : Vector {
    using Vector::Vector;
    UniformSample(Vector&& A):Vector(move(A)){}
    /// Returns the sum of the samples
    real sum() const { return ::sum(*this); }
    /// Returns the mean of the samples
    real mean() const;
    /// Returns the variance of the samples
    real variance() const;

    real scale=1; // Scales from integer sample indices to floating-point sample positions
};
/// Multiplies sample by a scalar
UniformSample operator*(real scalar, const UniformSample& A);
/// Computes square values
UniformSample sq(const UniformSample& A);
/// Adds two sample
UniformSample operator+(const UniformSample& A, const UniformSample& B);
/// Substracts two sample
UniformSample operator-(const UniformSample& A, const UniformSample& B);
/// Parses a uniformly sampled distribution from tab-separated values
UniformSample parseUniformSample(const string& file);
/// Converts a uniformly sampled distribution to tab-separated values
String toASCII(const UniformSample& A);

// UniformSample[]
/// Estimates mean distribution from distribution samples
UniformSample mean(const ref<UniformSample>& samples);

/// Represents a sample distribution by its histogram
struct UniformHistogram : UniformSample {
    using UniformSample::UniformSample;
    UniformHistogram(UniformSample&& A):UniformSample(move(A)){}
    /// Returns the number of samples represented by this histogram
    uint64 sampleCount() const { return UniformSample::sum(); }
    /// Returns the sum of the samples represented by the histogram
    real sum() const;
    /// Returns the mean of the samples represented by the histogram
    real mean() const { return sum()/sampleCount(); }
    /// Returns the variance of the samples represented by the histogram
    real variance() const;
    /// Returns the median of the samples represented by the histogram
    uint median() const;
};

// NonUniformSample

/// Non-uniformly sampled distribution
struct NonUniformSample : map<real, real> {
    using map<real, real>::map;
    /// Returns the sum of the samples
    real sum() const;
    /// Returns the minimal interval between samples
    real delta() const;
    /// Interpolates a value from the sample point (using nearest sample (0th order))
    real interpolate(real x) const;
};
/// Multiplies sample by a scalar
NonUniformSample operator*(real scalar, NonUniformSample&& A);
/// Computes absolute values
NonUniformSample abs(const NonUniformSample& A);
/// Scales both the variable and the values of a distribution to keep the same area
NonUniformSample scaleDistribution(float scalar, NonUniformSample&& A);
/// Parses a non-uniformly sampled distribution from tab-separated values
NonUniformSample parseNonUniformSample(const string& file);
/// Converts a non-uniformly sampled distribution to tab-separated values
String toASCII(const NonUniformSample& A);

/// Resamples non uniform distributions to a common (discarding original variable scale)
array<UniformSample> resample(const ref<NonUniformSample>& nonUniformSamples);

struct NonUniformHistogram : NonUniformSample {
    using NonUniformSample::NonUniformSample;
    NonUniformHistogram(NonUniformSample&& A):NonUniformSample(move(A)){}
    /// Returns the number of samples represented by this histogram
    uint64 sampleCount() const { return NonUniformSample::sum(); }
    /// Returns the sum of the samples represented by the histogram
    real sum() const;
    /// Returns the mean of the samples represented by the histogram
    real mean() const { return sum()/NonUniformSample::sum(); }
    /// Returns the variance of the samples represented by the histogram
    real variance() const;
};

/// Set of named scalars
typedef map<String, real> ScalarMap;

ScalarMap parseMap(const string& file);
/// Converts a scalar map to tab-separated values
String toASCII(const ScalarMap& A);
