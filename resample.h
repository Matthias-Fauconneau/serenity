#include "core.h"

struct Resampler {
    no_copy(Resampler)
    Resampler(){}
    /// Allocates buffers and generates filter to resample from \a sourceRate to \a targetRate
    /// \note sourceSize will be the maximum size which can be given at once to filter
    Resampler(uint channels, uint sourceRate, uint targetRate);
    /// Resamples \a sourceSize samples from \a source to \a targetSize samples in \a target
    /// \note If mix is true, resampled \a source is mixed with \a target (instead of overwriting the \a target buffer).
    /// \note \a sourceSize should be lesser or equal to \a sourceRate given in constructor
    template<bool mix=false> void filter(const float* source, uint sourceSize, float* target, uint targetSize);

    operator bool() const;
    ~Resampler();

    static constexpr int channelCount=2;
    uint sourceRate=0,targetRate=0;

    float* kernel=0; uint N=0;
    float* buffer[channelCount]={0,0}; uint bufferSize=0;

    int integerAdvance=0;
    int fractionalAdvance=0;
};
