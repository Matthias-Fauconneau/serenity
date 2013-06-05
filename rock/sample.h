#pragma once
#include "string.h"

/// Finite discrete integer-valued distribution (represented by its samples)
typedef buffer<double> Sample;

/// Sums all samples
uint64 sum(const Sample& A);

/// Multiplies sample by a scalar
Sample operator*(double s, const Sample& A);

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
