#pragma once
#include "memory.h"
#include "math.h"

/// Convenient interface to the FFTW library
#include <fftw3.h> //fftw3f
typedef struct fftwf_plan_s* fftwf_plan;
struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };
FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }
struct FFT {
    uint N;
    buffer<float> hann {N};
    buffer<float> windowed {N};
    buffer<float> halfcomplex {N};
    FFTW fftw = fftwf_plan_r2r_1d(N, windowed.begin(), halfcomplex.begin(), FFTW_R2HC, FFTW_ESTIMATE);
    FFT(uint N) : N(N) { assert(isPowerOfTwo(N)); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2; }
    ref<float> transform(const ref<float>& signal={}) {
        assert(N == signal.size);
        for(uint i: range(N)) windowed[i] = hann[i]*signal[i]; // Multiplies window
        fftwf_execute(fftw); // Transforms (FIXME: use execute_r2r and free windowed/transform buffers between runs)
        return halfcomplex;
    }
};

struct PitchEstimator : FFT {
    using FFT::FFT;
    buffer<float> spectrum {N/2};
    uint fPeak;
    float power;
    uint period;
    /// Returns fundamental period (non-integer when estimated without optimizing autocorrelation)
    float estimate(const ref<float>& signal, uint fMin=1, uint fMax=0) {
        if(!fMax) fMax = N/2;
        ref<float> halfcomplex = transform(signal);
        for(uint i: range(N/2)) spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum

        /// Estimator parameters
        const uint highPeakFrequency = min(fMax, N/16); // Minimum frequency to switch from global maximum to highest frequency selection
        const float highPeakRatio = 1./6; // Minimum energy (compared to global maximum) to be selected as an highest frequency peak
        const uint autocorrelationFrequency = N/16; // Maximum frequency to use autocorrelation optimization
        const uint highPartialFrequency = N/16; // Maximum frequency to consider peak being over 2nd partial and search for 3x, 4x, 5x the peak period
        const uint highPartialMaximumPeriods = 9; // Maximum peak period multiple to search when peak is under high partial frequency
        const uint lowPartialMaximumPeriods = 2; // Maximum peak period multiple to search when peak is over high partial frequency
        const float multiplePeriodPenalty = 1./64; // Penalty coefficient to avoid multiple periods when less periods match nearly as well
        const float extendedSearch = 32; // Extends search to all k closer than (maximum / extendedSearch) from maximum

        fPeak=0;
        {float firstPeak = 0; float energy=0;
            uint i=fMin; for(; i<highPeakFrequency; i++) if(spectrum[i] > spectrum[i-1]) break; // Descends to first local minimum
            for(; i<highPeakFrequency; i++) { // Selects maximum peak
                energy+=spectrum[i];
                if(spectrum[i] > firstPeak) firstPeak = spectrum[i], fPeak = i;
            }
            for(uint i=highPeakFrequency; i<N/2; i++) {
                energy+=spectrum[i];
                if(spectrum[i] > highPeakRatio*firstPeak) {
                    if(spectrum[i] > firstPeak) firstPeak = spectrum[i], fPeak = i;
                    if(i<fMax) fPeak = i;  // Selects highest peak [+23]
                }
            }
            power = energy / N;
        }
        if(fPeak==0) return nan;

        float bestK = (float)N/fPeak;
        if(fPeak < autocorrelationFrequency) { // High pitches are accurately found by spectrum peak picker [+58]
            float max=0;
            uint maximumPeriods = fPeak < highPartialFrequency ? highPartialMaximumPeriods : lowPartialMaximumPeriods; // [f<N/64: +4, 5th: +3]
            maximumPeriods = min(maximumPeriods, fPeak/fMin);
            period = 1;
            for(uint i=1; i <= maximumPeriods; i++) { // Search multiple periods
                int k0 = i*N/fPeak;
                int octaveBestK = k0;
                for(uint k=k0;k>N/fMax;k--) { // Evaluates slightly smaller periods [+22]
                    float sum=0; for(uint i: range(N-k)) sum += signal[i]*signal[k+i];
                    sum *= 1 - i*multiplePeriodPenalty; // Penalizes to avoid some period doubling (overly sensitive) [+2]
                    if(sum > max) max = sum, octaveBestK = k, bestK = k, period=i;
                    else if(k*extendedSearch<octaveBestK*(extendedSearch-1)) break; // Search beyond local minimums to match lowest notes [+22]
                }
            }
            for(int k=bestK+1;;k++) { // Scans forward (increasing k) until local maximum to estimate subkey pitch (+3)
                float sum=0; for(uint i: range(N-k)) sum += signal[i]*signal[k+i];
                if(sum > max) max = sum, bestK = k;
                else break;
            }
        }
        return bestK;
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+3)%12]+str(key/12-2); }

// H(s) = (s^2 + 1) / (s^2 + s/Q + 1)
// Biquad notch filter
struct Notch {
    float a1,a2,b0,b1,b2;
    Notch(float f, float bw) {
        real w0 = 2*PI*f;
        real alpha = sin(w0)*sinh(ln(2)/2*bw*w0/sin(w0));
        real a0 = 1 + alpha;
        a1 = -2*cos(w0)/a0, a2 = (1 - alpha)/a0;
        b0 = 1/a0, b1 = -2*cos(w0)/a0, b2 = 1/a0;
    }
    float x1=0, x2=0, y1=0, y2=0;
    float operator ()(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1, x1=x, y2=y1, y1=y;
        return y;
    }
};
