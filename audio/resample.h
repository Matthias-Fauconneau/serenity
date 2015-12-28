#pragma once
#include "memory.h"
#include "vector.h"

struct Resampler {
 Resampler(){}
 /// Allocates buffers and generates filter to resample from \a sourceRate to \a targetRate
 /// \note readSize will be the maximum size which can be read at once from filter
 Resampler(uint channels, uint sourceRate, uint targetRate, size_t readSize, size_t writeSize=0);
 /// Returns needed input size to produce a given target size
 int need(uint targetSize);
 /// Stores \a sourceSize samples to the resampling buffer
 void write(ref<float> source);
 /// Returns available output size
 size_t available();
 /// Convolves buffered samples with the resampling kernel to produce \a targetSize samples
 /// \note If mix is true, samples are mixed with \a target (instead of overwriting the \a target buffer).
 template<bool mix=false> void read(mref<float> target);
 /// Resamples \a sourceSize samples from \a source to \a targetSize samples in \a target
 /// \note If mix is true, samples are mixed with \a target (instead of overwriting the \a target buffer).
 template<bool mix=false> void filter(ref<float> source, mref<float> target);
 /// Clears input (buffer pointers)
 void clear();

 explicit operator bool() const { return (bool)kernel; }

 uint channels = 0;
 uint sourceRate = 1, targetRate = 1;

 buffer<float> kernel; uint N=0;
 buffer<float> signal[2]; uint bufferSize=0;

 size_t writeIndex=0;
 size_t integerAdvance=0, fractionalAdvance=0;
 size_t integerIndex=0, fractionalIndex=0;
};
