#include "fft.h"
#include "math.h"
#include <fftw3.h> //fftw3f fftw3f_threads
#include "string.h"

__attribute((constructor(1001))) void initialize_FFTW() {
    fftwf_init_threads();
    fftwf_plan_with_nthreads(4);
}

FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }

FFT::FFT(uint N) : N(N), fftw(fftwf_plan_r2r_1d(N, windowed.begin(), halfcomplex.begin(), FFTW_R2HC, FFTW_ESTIMATE)) {
    //for(uint i: range(N)) { const double z = 2*PI*i/N; window[i] = 1 - 1.90796*cos(z) + 1.07349*cos(2*z) - 0.18199*cos(3*z); }
    for(uint i: range(N)) { const double z = 2*PI*i/(N-1); const double a=0.53836; window[i] = a  - (1-a)*cos(z); }
}

mref<float> FFT::transform() {
    fftwf_execute(fftw); // Transforms (FIXME: use execute_r2r and free windowed/transform buffers between runs)
    float energy=0;
    for(uint i: range(N/2)) {
        spectrum[i] = (sq(halfcomplex[i]) + sq(halfcomplex[N-1-i])) / N; // Converts amplitude to power spectrum density
        energy += spectrum[i];
    }
    periodEnergy = energy;
    periodPower = energy / (N/2);
    return spectrum;
}
