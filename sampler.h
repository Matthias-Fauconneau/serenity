#pragma once
/// \file sampler.h High performance, low latency SFZ sound font sampler
#include "function.h"
#include "file.h"
#include "resample.h"
#include "flac.h"
#include "process.h"
#include <fftw3.h>

typedef float float4 __attribute((vector_size(16)));
struct Note : FLAC {
    Note(){}
    Note(const ref<byte>& o):FLAC(o){}
    float4 level; //current note attenuation
    float4 step; //coefficient for release fade out = (2 ** -24)**(1/releaseTime)
    Semaphore readCount; //decoder thread releases decoded samples, audio thread acquires
    Semaphore writeCount __((int)buffer.capacity); //audio thread release free samples, decoder thread acquires
    uint16 releaseTime; //to compute step
    uint8 key=0; //to match release sample
    ref<float> envelope; //to level release sample
    /// Decodes frames until \a available samples is over \a need
    void decode(uint need);
    /// Reads \a size samples to be mixed into \a out and returns true when decayed
    void read(float4* out, uint size);
    /// Computes sum of squares on the next \a size samples (to compute envelope)
    float sumOfSquares(uint size);
    /// Computes actual sound level on the next \a size samples (using precomputed envelope)
    float actualLevel(uint size) const;
};

struct Sample {
    Map map; Note data; array<float> envelope; //Sample data
    int16 trigger=0; int16 lovel=0; int16 hivel=127; int16 lokey=0; int16 hikey=127; //Input controls
    int16 pitch_keycenter=60; uint16 releaseTime=0; int16 amp_veltrack=100; /*int16 rt_decay=0;*/ int16 volume=1; //Performance parameters
};

/// High performance, low latency SFZ sound font sampler
struct Sampler : Poll {
    ~Sampler();

    /// Opens a .sfz instrument and maps all its samples
    void open(const ref<byte>& path); array<Sample> samples;

    /// Receives MIDI note events
    void noteEvent(int key, int velocity);
    Lock noteReadLock; // Prevent decoder to edit note arrays while mixing
    Lock noteWriteLock; // Prevent input to realloc note while decoding
    array<Note> notes[3]; // Active notes (currently being sampled)

    /// Callback to decode samples
    void event() override;

    /// Audio callback mixing each layers active notes, resample the shifted layers and mix them together to the audio buffer
    bool read(ptr& swPointer, int32* output, uint size);
    Resampler resampler[2];
    uint64 rate = 0;
    static constexpr uint periodSize = 1024;
    float* buffer; // Interleaved mixing buffer

    /// Convolution reverb
    uint reverbSize=0; // Reverb filter size
    uint N=0; // reverbSize+periodSize
    float* reverbFilter[2]={}; // Convolution reverb filter in frequency-domain
    float* reverbBuffer[2]={}; // Mixer output in time-domain

    //uint reverbIndex=0; //ring buffer index TODO
    float* input=0; // Buffer to hold transform of reverbBuffer
    float* product=0; // Buffer to hold multiplication of signal and reverbFilter

    fftwf_plan forward[2]; // FFTW plan to forward transform reverb buffer
    fftwf_plan backward; // FFTW plan to backward transform product*/

    /// Emits period time to trigger MIDI file input and update the interface
    signal<uint /*delta*/> timeChanged;
    uint64 lastTime=0, time=0, recordStart=0;

    /// Records performance to WAV file
    void startRecord(const ref<byte>& path);
    void stopRecord();
    File record=0;
    signal<float* /*data*/, uint /*size*/> frameReady;

    operator bool() const { return samples.size(); }
};
