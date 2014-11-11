#pragma once
/// \file sampler.h High performance, low latency SFZ sound font sampler
#include "function.h"
#include "file.h"
#include "resample.h"
#include "flac.h"
#include "thread.h"
#include "map.h"
#include "simd.h"
#if REVERB
typedef struct fftwf_plan_s* fftwf_plan;
#endif

/// High performance, low latency SFZ sound font sampler
struct Sampler : Poll {
	struct Sample;
	struct Layer;

	/// Samples composing the current instrument
	array<Sample> samples;

	Semaphore lock {2}; // Decoder (producer) and mixer (consumer) may use \a layers concurrently, a mutator needs to lock/acquire both
    array<Layer> layers;

	uint64 rate = 0;
    //static constexpr uint periodSize = 64; // [1ms] Prevents samples to synchronize with shifted copies from same chord
    //static constexpr uint periodSize = 128; // [3ms] Same as resampler latency and 1m sound propagation time
    //static constexpr uint periodSize = 256; // [5ms] Latency/convolution tradeoff (FIXME: ring buffer)
    //static constexpr uint periodSize = 512; // [11ms] Required for efficient FFT convolution (reverb) (FIXME: ring buffer)
	//static constexpr uint periodSize = 1024; // [21ms] Maximum compatibility (when latency is not critical) (FIXME: skip start for accurate timing))
	static constexpr uint periodSize = 1024; // [42ms] FIXME

    /// Convolution reverb
	bool enableReverb=false; // Disables reverb by default as it prevents lowest latency (FFT convolution gets too expensive).
    uint N=0; // reverbSize+periodSize
#if REVERB
    buffer<float> reverbFilter[2]; // Convolution reverb filter in frequency-domain
    buffer<float> reverbBuffer[2]; // Mixer output in time-domain

    //uint reverbIndex=0; //ring buffer index TODO
    buffer<float> input; // Buffer to hold transform of reverbBuffer
    buffer<float> product; // Buffer to hold multiplication of signal and reverbFilter

    struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };
    FFTW forward[2]; // FFTW plan to forward transform reverb buffer
    FFTW backward; // FFTW plan to backward transform product*/
#endif

    /// Emits period time to trigger MIDI file input and update the interface
	function<void(uint64)> timeChanged;
    uint64 time=0, stopTime=0;

	/// Whether decoding is run in advance in main thread.
	/// \note Prevents underruns when latency is much lower than FLAC frame sizes.
	///          FLAC frames need to be fully decoded in order to get both channels.
	bool backgroundDecoder;

	explicit operator bool() const { return samples.size; }

	Sampler(uint outputRate, string path, function<void(uint64)> timeChanged, Thread& thread=mainThread);
	~Sampler();

	void noteEvent(uint key, uint velocity);

    /// Callback to decode samples
    void event() override;

    /// Audio callback mixing each layers active notes, resample the shifted layers and mix them together to the audio buffer
	size_t read(mref<int2> output);
	size_t read(mref<float2> output);

    /// Signals when all samples are done playing
	//signal<> silence;
	bool silence = false;
};
