#include "core.h"

struct Resampler {
    no_copy(Resampler);
    Resampler(){}
    /// Allocates buffers and generates filter to resample from \a sourceRate to \a targetRate
    /// \note bufferSize will be the maximum size which can be given at once to filter
    Resampler(uint channels, uint sourceRate, uint targetRate, uint bufferSize=0);
    /// Returns needed input size to produce a given target size
    int need(uint targetSize);
    /// Stores \a sourceSize samples to the resampling buffer
    void write(const float* source, uint sourceSize);
    /// Convolves buffered samples with the resampling kernel to produce \a targetSize samples
    /// \note If mix is true, samples are mixed with \a target (instead of overwriting the \a target buffer).
    template<bool mix> void read(float* target, uint targetSize);
    /// Resamples \a sourceSize samples from \a source to \a targetSize samples in \a target
    /// \note If mix is true, samples are mixed with \a target (instead of overwriting the \a target buffer).
    template<bool mix> void filter(const float* source, uint sourceSize, float* target, uint targetSize);

    explicit operator bool() const { return kernel; }
    ~Resampler();

    static constexpr uint channelCount=2;
    uint sourceRate=0,targetRate=0;

    float* kernel=0; uint N=0;
    float* buffer[channelCount]={0,0}; uint bufferSize=0;

    uint writeIndex=0;
    uint integerAdvance=0, fractionalAdvance=0;
    uint integerIndex=0, fractionalIndex=0;
};
