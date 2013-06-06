#pragma once
#include "string.h"

/// Finite uniformly sampled distribution
typedef buffer<float> Sample; //TODO: non-uniform sampling (using map<float,float>)

/// Returns the sum of the samples
float sum(const Sample& A);

/// Returns the mean of the samples
float mean(const Sample& A);

/// Returns the variance of the samples
float variance(const Sample& A);

/// Returns the sum of the samples represented by the histogram
float histogramSum(const Sample& A);

/// Returns the mean of the samples represented by the histogram
float histogramMean(const Sample& A);

/// Returns the variance of the samples represented by the histogram
float histogramVariance(const Sample& A);

/// Multiplies sample by a scalar
Sample operator*(float s, const Sample& A);

/// Substracts samples clipping to zero
Sample operator-(const Sample& A, const Sample& B);

/// Multiplies samples
Sample operator*(const Sample& A, const Sample& B);

/// Square roots and round X coordinates summing all sample falling in the same bin
Sample sqrtHistogram(const Sample& A);

Sample parseSample(const ref<byte>& file);

string toASCII(const Sample& sample, float scale=1);

/// Cauchy-Lorentz distribution 1/(1+x²)
struct Lorentz {
    float position, height, scale;
    float operator[](float x) const { return height/(1+sq((x-position)/scale)); }
};
template<> inline string str(const Lorentz& o) { return "x₀ "_+str(o.position)+", I"_+str(o.height)+", γ "_+str(o.scale); }

/// Estimates parameters for a Lorentz distribution fitting the maximum peak
Lorentz estimateLorentz(const Sample& sample);
/// Evaluates a Lorentz distribution at regular intervals
Sample sample(const Lorentz& lorentz, uint size);
/// Computes the intersection of two Lorentz distributions
//float intersect(const Lorentz& a, const Lorentz& b);
