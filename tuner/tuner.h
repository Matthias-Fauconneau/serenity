#pragma once
#include "function.h"
#include "widget.h"
typedef struct fftwf_plan_s* fftwf_plan;

struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };

struct Spectrum : Widget {
    Spectrum(uint sampleRate, uint periodSize);
    uint write(int16* data, uint size);
    uint write(int32* data, uint size);
    uint write(uint size);
    void render(int2 position, int2 size);

    ::signal<> contentChanged;

    const uint N = 16384; // Discrete fourier transform size (dF ~ 5.8Hz)
    uint sampleRate, periodSize;

    buffer<float> signal; // Original signal
    buffer<float> hann; // Hann window
    buffer<float> windowed; // Windowed frame of signal
    buffer<float> transform; // Fourier transform of the windowed buffer
    buffer<float> spectrum; // Magnitude of the complex Fourier coefficients
    float maximumBandPower=1; // Time smoothed maximum of each band (note) power
    float maximumFrequency=0; // Frequency of the maximum peak (on the last period) (in key)
    uint dts=0, pts=0;
    FFTW fftw;
};
