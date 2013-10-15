#pragma once
/// \file sampler.h High performance, low latency SFZ sound font sampler
#include "function.h"
#include "file.h"
#include "resample.h"
#include "flac.h"
#include "thread.h"
#include "map.h"
#include "simd.h"
typedef struct fftwf_plan_s* fftwf_plan;

struct Note {
    default_move(Note);
    explicit Note(FLAC&& flac):flac(move(flac)),readCount(this->flac.audio.size),writeCount((int)this->flac.audio.capacity-this->flac.audio.size){}
    FLAC flac;
    v4sf level; //current note attenuation
    v4sf step; //coefficient for release fade out = (2 ** -24)**(1/releaseTime)
    Semaphore readCount; //decoder thread releases decoded samples, audio thread acquires
    Semaphore writeCount; //audio thread release free samples, decoder thread acquires
    uint16 releaseTime; //to compute step
    uint8 key=0, velocity=0; //to match release sample
    ref<float> envelope; //to level release sample
    /// Decodes frames until \a available samples is over \a need
    void decode(uint need);
    /// Reads \a size samples to be mixed into \a out and returns true when decayed
    void read(v4sf* out, uint size);
    /// Computes sum of squares on the next \a size samples (to compute envelope)
    float sumOfSquares(uint size);
    /// Computes actual sound level on the next \a size samples (using precomputed envelope)
    float actualLevel(uint size) const;
};

struct Sample {
    Map map; FLAC flac; array<float> envelope; //Sample data
    int16 trigger=0; uint16 lovel=0; uint16 hivel=127; uint16 lokey=0; uint16 hikey=127; //Input controls
    int16 pitch_keycenter=60; uint16 releaseTime=0; int16 amp_veltrack=/*100*/0; float volume=1; //Performance parameters
};
inline String str(const Sample& s) { return str(s.lokey)+"-"_+str(s.pitch_keycenter)+"-"_+str(s.hikey); }

/// High performance, low latency SFZ sound font sampler
struct Sampler : Poll {
    Lock lock; // Prevents decoder from removing notes being mixed
    struct Layer {
        float shift;
        array<Note> notes; // Active notes (currently being sampled) in this layer
        Resampler resampler; // Resampler to shift pitch
        buffer<float> audio; // Buffer to mix notes before resampling
    };
    array<Layer> layers;

    uint rate = 0;
    //static constexpr uint periodSize = 64; // [1ms] Prevents samples to synchronize with shifted copies from same chord
    //static constexpr uint periodSize = 128; // [3ms] Same as resampler latency and 1m sound propagation time
    //static constexpr uint periodSize = 256; // [5ms] Latency/convolution tradeoff (FIXME: ring buffer)
    static constexpr uint periodSize = 512; // [11ms] Required for efficient FFT convolution (reverb) (FIXME: ring buffer)
    //static constexpr uint periodSize = 1024; // [21ms] Maximum compatibility (when latency is not critical) (FIXME: skip start for accurate timing))

    /// Convolution reverb
    bool enableReverb=true; // Disable reverb by default as it prevents lowest latency (FFT convolution gets too expensive).
    uint reverbSize=0; // Reverb filter size
    uint N=0; // reverbSize+periodSize
    buffer<float> reverbFilter[2]; // Convolution reverb filter in frequency-domain
    buffer<float> reverbBuffer[2]; // Mixer output in time-domain

    //uint reverbIndex=0; //ring buffer index TODO
    buffer<float> input; // Buffer to hold transform of reverbBuffer
    buffer<float> product; // Buffer to hold multiplication of signal and reverbFilter

    struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };
    FFTW forward[2]; // FFTW plan to forward transform reverb buffer
    FFTW backward; // FFTW plan to backward transform product*/

    /// Emits period time to trigger MIDI file input and update the interface
    signal<uint /*delta*/> timeChanged;
    uint64 lastTime=0, time=0, recordStart=0, stopTime=0;

    ~Sampler();

    /// Opens a .sfz instrument and maps all its samples
    void open(uint outputRate, const string& path, const Folder& root=::root());
    array<Sample> samples;

    /// Receives MIDI note events
    void noteEvent(uint key, uint velocity);

    /// Callback to decode samples
    void event() override;

    /// Audio callback mixing each layers active notes, resample the shifted layers and mix them together to the audio buffer
    uint read(int32* output, uint size);

    /// Records performance to WAV file
    void startRecord(const string& path);
    void stopRecord();
    File record {0};
    signal<float* /*data*/, uint /*size*/> frameReady;

    operator bool() const { return samples.size; }
};
