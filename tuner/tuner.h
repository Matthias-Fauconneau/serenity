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
    uint dts=0, pts=0; // Processed/Displayed frame count

    const uint N = 8192; // Discrete fourier transform size (dF ~ 5.9Hz)
    uint sampleRate, periodSize;

    buffer<float> signal; // Original signal
    buffer<float> hann; // Hann window
    buffer<float> windowed; // Windowed frame of signal
    buffer<float> transform; // Fourier transform of the windowed buffer
    buffer<float> spectrum; // Magnitude of the complex Fourier coefficients
    FFTW fftw;

    buffer<float> harmonic; // Spectral harmonic correlation
    const int smoothRadius = 1; // Sums R+1+R frequency bins to smooth spectral harmonic correlation evaluation
    const uint octaveCount = 3; // Number of octaves to multiply
    float maximum=1; // Maximum value of the spectral harmonic correlation
    float f0=0; // Estimation of the fundamental frequency (Argument maximum of the spectral harmonic correlation) (~pitch)

};
