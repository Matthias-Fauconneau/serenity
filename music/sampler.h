#pragma once
/// \file sampler.h High performance, low latency SFZ sound font sampler
#include "function.h"
#include "file.h"
#include "flac.h"
#include "thread.h"
#include "map.h"
#include "simd.h"
#include "time.h"

/// High performance, low latency SFZ sound font sampler
struct Sampler : Poll {
    struct Sample;
    struct Layer;

    /// Samples composing the current instrument
    array<Sample> samples;

    Lock noteReferencesLock; // When cleaning notes in mixer thread, locks decoding before note references are reset
    //Lock notesSizeLock; // When cleaning notes in mixer thread, locks appending new notes before array end is reset (no need as new notes are assumed to be added in mixer thread)
    // Using two locks as new notes may be added while decoding (except on realloc)

    array<Layer> layers;

    Random random;
    uint cc64 = 0;

    static constexpr uint channels = 2;
    uint rate = 0;
    const uint periodSize;
    const bool realTime = true; //periodSize < 1024; // Whether to output decoder underrun warnings

    /// Just before samples are mixed, polls note events
    function<void(uint)> timeChanged;
    uint audioTime=0, stopTime=0;
    //float minValue = -64577408, maxValue = 76015472;
    float minValue = -266941, maxValue = 259313;
    explicit operator bool() const { return samples.size; }

    Sampler(string path, const uint periodSize/*[12ms/82Hz/4m]*/, function<void(uint)> timeChanged={}, Thread& thread=mainThread);
    virtual ~Sampler();

    void ccEvent(uint key, uint value);
    void noteEvent(uint key, uint velocity/*, float2 gain*/);

    /// Callback to decode samples
    void event() override;

    /// Audio callback mixing each layers active notes, resample the shifted layers and mix them together to the audio buffer
    size_t read32(mref<int2> output);
    size_t read16(mref<short2> output);
    size_t read(mref<float2> output);

    /// Signals when all samples are done playing
    bool silence = false;
};
