#pragma once
#include "memory.h"

/// Convenient interface to the FFTW library
typedef struct fftwf_plan_s* fftwf_plan;
struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };

struct FFT {
    const uint N;
    buffer<float> window {N};
    buffer<float> windowed {N};
    buffer<float> halfcomplex {N};
    FFTW fftw;
    /// Setups an FFT of size N (with a flat top window).
    FFT(uint N);
    buffer<float> spectrum {N/2}; // Power spectrum
    float periodEnergy; //  Spectral energy of the last period
    float periodPower; // Mean spectral energy density over the last period
    /// Executes an FFT transform and computes power spectrum
    mref<float> transform();
};
