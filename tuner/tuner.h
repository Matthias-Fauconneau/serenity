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

    const uint N = 32768; // Discrete fourier transform size (dF ~ 1.5Hz, dT ~ 0.7s)
    uint sampleRate, periodSize;

    buffer<float> signal; // Original signal
    buffer<float> hann; // Hann window
    buffer<float> windowed; // Windowed frame of signal
    buffer<float> transform; // Fourier transform of the windowed buffer
    buffer<float> spectrum; // Magnitude of the complex Fourier coefficients
    FFTW fftw;
    float spectrumMaximum=1; // Current maximum value of the spectrum

    buffer<float> harmonic; // Spectral harmonic correlation
    const int smoothRadius = 1; // Sums R+1+R frequency bins to smooth spectral harmonic correlation evaluation
    const uint octaveCount = 3; // Number of octaves to multiply
    float maximum=1; // Current maximum value of the spectral harmonic correlation
    float f0=0; // Current estimation of the fundamental frequency (Argument maximum of the spectral harmonic correlation) (~pitch)
    const float threshold = 0x1p-25; // SPH threshold to estimate pitch (FIXME: relative)
    static constexpr uint medianSize = 7;
    float pitches[medianSize] = {}; // F0 estimation for the last 7 frames
    float pitch=0; // Current pitch estimation (median of last frames)
};
