#pragma once
#include "volume.h"

/// Sample of a positive distribution
typedef array<int> Sample;

/// Computes histogram of values (without normalizing)
Sample histogram(const Volume16& volume, bool cylinder=false);

/// Computes histogram of square roots (without normalizing)
Sample sqrtHistogram(const Volume16& volume, bool cylinder=false);

Sample parseSample(const ref<byte>& file);

string toASCII(const Sample& sample, float scale=1);

/// Lorentz distribution 1/(1+x²)
struct Lorentz {
    float position, height, scale;
    float operator[](float x) const { return height/(1+sqr((x-position)/scale)); }
};
/// Estimates parameters for a Lorentz distribution fitting the maximum peak
Lorentz estimateLorentz(const Sample& sample);
/// Evaluates a Lorentz distribution at regular intervals
Sample sample(const Lorentz& lorentz, uint size);
/// Computes the intersection of two Lorentz distributions
float intersect(const Lorentz& a, const Lorentz& b);

/// Substracts two samples clipping to zero
Sample operator-(const Sample& A, const Sample& B);
