#pragma once
#include "function.h"
#include "file.h"
#include "resample.h"
#include "flac.h"
#include "process.h"

typedef float float4 __attribute((vector_size(16)));
struct Note : FLAC {
    Note(){}
    Note(const ref<byte>& o):FLAC(o){}
    float4 level; //current note attenuation
    float4 step; //coefficient for release fade out = (2 ** -24)**(1/releaseTime)
    Semaphore readCount; //decoder thread releases decoded samples, audio thread acquires
    Semaphore writeCount __(1<<16); //audio thread release free samples, decoder thread acquires
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
    Map map; Note cache; array<float> envelope; //Sample data
    int16 trigger=0; int16 lovel=0; int16 hivel=127; int16 lokey=0; int16 hikey=127; //Input controls
    int16 pitch_keycenter=60; uint16 releaseTime=0; int16 amp_veltrack=100; /*int16 rt_decay=0;*/ int16 volume=1; //Performance parameters
};

struct Sampler : Poll {
    /// Opens a .sfz instrument and maps all its samples
    void open(const ref<byte>& path);
    array<Sample> samples;
    uint predecode=0; signal<int, int> progressChanged; //decode start buffer for all samples

    /// Receives MIDI note events
    void noteEvent(int key, int velocity);
    Lock noteReadLock; // Prevent decoder to edit note arrays while mixing
    Lock noteWriteLock; // Prevent input to realloc note while decoding
    array<Note> notes[3]; // Active notes (currently being sampled)

    /// Callback to decode samples
    void event();

    /// Audio callback mixing each layers active notes, resample the shifted layers and mix them together to the audio buffer (TODO: reverb)
    bool read(int16* output, uint size);
    Resampler resampler[2];

    /// Emits period time to update the interface
    signal<int> timeChanged;

    /// Records performance to WAV file
    void recordWAV(const ref<byte>& path); File record=0; int16* pcm = 0; int time = 0; ~Sampler();

    operator bool() const { return samples.size(); }
};
