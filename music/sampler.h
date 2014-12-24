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

/// High performance, low latency SFZ sound font sampler
struct Sampler : Poll {
	struct Sample;
	struct Layer;

	/// Samples composing the current instrument
	array<Sample> samples;

	Semaphore lock {2}; // Decoder (producer) and mixer (consumer) may use \a layers concurrently, a mutator needs to lock/acquire both
    array<Layer> layers;

	static constexpr uint channels = 2;
	uint rate = 0;
    //static constexpr uint periodSize = 64; // [1ms] Prevents samples to synchronize with shifted copies from same chord
    //static constexpr uint periodSize = 128; // [3ms] Same as resampler latency and 1m sound propagation time
    static constexpr uint periodSize = 256; // [5ms] Latency/convolution tradeoff (FIXME: ring buffer)
    //static constexpr uint periodSize = 512; // [11ms] Required for efficient FFT convolution (reverb) (FIXME: ring buffer)

#if 1
    /// Convolution reverb
    uint N=0; // reverbSize+periodSize
    buffer<float> reverbFilter[2]; // Convolution reverb filter in frequency-domain
    buffer<float> reverbBuffer[2]; // Mixer output in time-domain

    //uint reverbIndex=0; //ring buffer index TODO
    buffer<float> input; // Buffer to hold transform of reverbBuffer
    buffer<float> product; // Buffer to hold multiplication of signal and reverbFilter

    struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };
    FFTW forward[2]; // FFTW plan to forward transform reverb buffer
    FFTW backward; // FFTW plan to backward transform product*/
#else
#endif

    /// Emits period time to trigger MIDI file input and update the interface
	function<void(uint)> timeChanged;
	uint audioTime=0, stopTime=0;

	explicit operator bool() const { return samples.size; }

	Sampler(uint outputRate, string path, function<void(uint)> timeChanged, Thread& thread=mainThread);
	~Sampler();

	void noteEvent(uint key, uint velocity);

    /// Callback to decode samples
    void event() override;

    /// Audio callback mixing each layers active notes, resample the shifted layers and mix them together to the audio buffer
	size_t read32(mref<int2> output);
	size_t read16(mref<int16> output);
	size_t read(mref<float2> output);

    /// Signals when all samples are done playing
	//signal<> silence;
	bool silence = false;
};
