#pragma once
#include "memory.h"
#include "math.h"
#include "time.h"
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

#include "simd.h"
float correlation(const float* a, const float* b, uint N) {
    v4sf sum = {0,0,0,0};
    for(uint i=0; i<=N-4; i+=4) sum += loadu(a+i) * loadu(b+i); // FIXME: align one, align both when possible
    return sum[0]+sum[1]+sum[2]+sum[3];
}

float autocorrelation(const float* x, uint k, uint N) { return correlation(x,x+k,N-k) / (N-k); }

struct PitchEstimator : FFT {
    using FFT::FFT;
    buffer<float> spectrum {N/2};
    buffer<float> autocorrelations;
    buffer<float> harmonicProducts;
    uint fPeak;
    float power;
    uint period;
    /// Returns fundamental period (non-integer when estimated without optimizing autocorrelation)
    /// \a fMin Minimum frequency for maximum peak selection (autocorrelation is still allowed to match lower pitches)
    /// \a fMax Maximum frequency for highest peak selection (maximum peak is still allowed to select higher pitches)
    float estimate(const ref<float>& signal, uint fMin, uint fMax, uint kMax) {
        //if(!kMax) kMax=N/2; // Needs at least two periods for autocorrelation
        //if(!fMax) fMax = N/2; // Up to the critical frequency
        //assert_(kMax <= N/2 && fMax <= N/2, kMax, fMax);
        ref<float> halfcomplex = transform(signal);
        for(uint i: range(N/2)) spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum
#if 0
        /// Estimator parameters
        const uint highPeakFrequency = min(fMax, N/16); // Minimum frequency to switch from global maximum to highest frequency selection
        const float highPeakRatio = 1./6; // Minimum energy (compared to global maximum) to be selected as an highest frequency peak
        const uint autocorrelationFrequency = N/8; // Maximum frequency to use autocorrelation optimization
        const uint maximumPeriods = 23; // Maximum peak period multiple to search

        fPeak=0;
        {float firstPeak = 0; float energy=0;
            uint i = fMin; for(; i<highPeakFrequency; i++) if(spectrum[i-1] < spectrum[i]) break; // Descends to first local minimum
            for(; i<highPeakFrequency; i++) { // Selects maximum peak
                energy+=spectrum[i];
                if(spectrum[i] > firstPeak) firstPeak = spectrum[i], fPeak = i;
            }
            for(uint i : range(highPeakFrequency, N/2)) {
                energy+=spectrum[i];
                if(spectrum[i] > highPeakRatio*firstPeak) {
                    if(spectrum[i] > firstPeak) firstPeak = spectrum[i], fPeak = i;
                    if(i<fMax) fPeak = i;  // Selects highest peak [+23]
                }
            }
            power = energy / sq((N/2-fMin));
        }
        if(fPeak==0) return nan;

        float globalK = (float)N/fPeak;
        if(fPeak < autocorrelationFrequency) { // High pitches are accurately found by spectrum peak picker [+58]
            autocorrelations = buffer<float>(kMax);
            clear(autocorrelations.begin(), kMax);

            harmonicProducts = buffer<float>(12*fMax);
            clear(harmonicProducts.begin(), 12*fMax);

            float globalMax=0;
            for(uint i=1; i <= min(maximumPeriods, fPeak*kMax/N); i++) { // Evaluate multiple periods
                float localMax = 0;
                /*uint k0 = i*N/fPeak; //round(i*N/(fPeak+0.5));
                const uint kMin = N/fMax;
                // Autocorrelation optimization
                uint localK = k0;
                for(uint k=k0; k > kMin; k--) {  // Scans backward (increasing frequency) until local maximum
                    float sum = autocorrelation(signal, k, N);
                    autocorrelations[k] = sum;
                    if(sum > localMax) localMax=sum, localK=k;
                    else break;
                }
                for(uint k=k0+1; k < kMax; k++) { // Scans forward (decreasing frequency) until local maximum
                    float sum = autocorrelation(signal, k, N);
                    autocorrelations[k] = sum;
                    if(sum > localMax) localMax=sum, localK=k;
                    else break;
                }
                if(localMax > globalMax) globalMax=localMax, globalK=localK, period=i;
                // Harmonic product spectrum weight
                for(uint n : range(2,12)) { uint i = n*N/localK; localMax *= spectrum[i]; }
                //for(uint n : range(2,12)) { uint i = n*N/localK; localMax *= spectrum[i-1]+spectrum[i]+spectrum[i+1]; }*/

                // Direct harmonic product spectrum optimization
                uint localF = fPeak;
                for(uint f=fPeak; f > fMin; f--) { // Scans backward (decreasing frequency) until local maximum
                    float product=1; for(uint n : range(2,12)) product *= spectrum[n*f/i];
                    harmonicProducts[12*f/i] = product;
                    if(product > localMax) localMax=product, localF=f;
                    else break;
                }
                for(uint f=fPeak; f < fMax; f++) { // Scans forward (increasing frequency) until local maximum
                    float product=1; for(uint n : range(2,12)) product *= spectrum[n*f/i];
                    harmonicProducts[12*f/i] = product;
                    if(product > localMax) localMax=product, localF=f;
                    else break;
                }
                if(localMax > globalMax) globalMax=localMax, globalK=i*N/localF, period=i;
            }

        }
        return globalK;
#else
        float energy=0; //TODO: harmonic energy
        for(uint i: range(fMin, N/2)) energy+=spectrum[i];
        power = energy / sq(N/2-fMin);

        harmonicProducts = buffer<float>(12*fMax);
        clear(harmonicProducts.begin(), 12*fMax);
        float max=0; uint hpsPeak=0;
        for(uint i: range(fMin*12, min(12*fMax,N/2))) {
            float product=1; for(uint n : range(2,12)) product *= spectrum[n*i/12];
            harmonicProducts[i] = product;
            if(product > max) max=product, hpsPeak = i;
        }
        period = 1;
        return 12.*N/hpsPeak;
#endif
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+3)%12]+str(key/12-2); }

// H(s) = (s^2 + 1) / (s^2 + s/Q + 1)
// Biquad notch filter
struct Notch {
    real frequency, bandwidth;
    real a1,a2,b0,b1,b2;
    Notch(real f, real bw) : frequency(f), bandwidth(bw) {
        real w0 = 2*PI*f;
        real alpha = sin(w0)*sinh(ln(2)/2*bw*w0/sin(w0));
        real a0 = 1 + alpha;
        a1 = -2*cos(w0)/a0, a2 = (1 - alpha)/a0;
        b0 = 1/a0, b1 = -2*cos(w0)/a0, b2 = 1/a0;
    }
    real x1=0, x2=0, y1=0, y2=0;
    real operator ()(real x) {
        real y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1, x1=x, y2=y1, y1=y;
        return y;
    }
};
