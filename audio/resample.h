#pragma once
#include "memory.h"
#include "vector.h"

struct Resampler {
    Resampler(){}
    /// Allocates buffers and generates filter to resample from \a sourceRate to \a targetRate
    /// \note bufferSize will be the maximum size which can be given at once to filter
    Resampler(uint channels, uint sourceRate, uint targetRate, uint bufferSize=0);
    /// Returns needed input size to produce a given target size
    int need(uint targetSize);
    /// Stores \a sourceSize samples to the resampling buffer
    void write(const ref<float2>& source);
    /// Returns available output size
    size_t available();
    /// Convolves buffered samples with the resampling kernel to produce \a targetSize samples
    /// \note If mix is true, samples are mixed with \a target (instead of overwriting the \a target buffer).
    template<bool mix=false> void read(const mref<float2>& target);
    /// Resamples \a sourceSize samples from \a source to \a targetSize samples in \a target
    /// \note If mix is true, samples are mixed with \a target (instead of overwriting the \a target buffer).
    template<bool mix=false> void filter(const ref<float2>& source, const mref<float2>& target);
    /// Clears input (buffer pointers)
    void clear();

    explicit operator bool() const { return (bool)kernel; }

    static constexpr uint channels=2;
    uint sourceRate=1,targetRate=1;

    buffer<float> kernel; uint N=0;
    buffer<float> signal[channels]; uint bufferSize=0;

    uint writeIndex=0;
    uint integerAdvance=0, fractionalAdvance=0;
    uint integerIndex=0, fractionalIndex=0;
};
