#pragma once
#include "math.h"
#include "time.h"
/// Convenient interface to the FFTW library
#include <fftw3.h> //fftw3f
typedef struct fftwf_plan_s* fftwf_plan;
struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };
FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }
struct FFT {
    const uint N;
    buffer<float> window {N};
    buffer<float> windowed {N};
    buffer<float> halfcomplex {N};
    FFTW fftw = fftwf_plan_r2r_1d(N, windowed.begin(), halfcomplex.begin(), FFTW_R2HC, FFTW_ESTIMATE);
    FFT(uint N) : N(N) {
        assert(isPowerOfTwo(N));
        for(uint i: range(N)) { const real z = 2*PI*i/N; window[i] = 1 - 1.90796*cos(z) + 1.07349*cos(2*z) - 0.18199*cos(3*z); }
    }
    ref<float> transform(const ref<float>& signal={}) {
        assert(N == signal.size);
        for(uint i: range(N)) windowed[i] = window[i]*signal[i]; // Multiplies window
        fftwf_execute(fftw); // Transforms (FIXME: use execute_r2r and free windowed/transform buffers between runs)
        return halfcomplex;
    }
};

template<Type V, uint N> struct list { // Small sorted list
    static constexpr uint size = N;
    struct element : V { float key; element(float key=0, V value=V()):V(value),key(key){} } elements[N];
    void clear() { ::clear(elements, N); }
    void insert(float key, V value) {
        int i=0; while(i<N && elements[i].key<key) i++; i--;
        assert_(key!=elements[i].key);
        if(i<0) return; // New candidate would be lower than current
        for(uint j: range(i)) elements[j]=elements[j+1]; // Shifts left
        elements[i] = element(key, value); // Inserts new candidate
    }
    const element& operator[](uint i) const { assert(i<N); return elements[i]; }
    const element& last() const { return elements[N-1]; }
    const element* begin() const { return elements; }
    const element* end() const { return elements+N; }
};

struct PitchEstimator : FFT {
    using FFT::FFT;
    const uint H = 33; //FIXME: 12-24
    buffer<float> spectrum {N/2};
    //buffer<float> smooth {N/2};
    struct Candidate { float f0; float B; int df; Candidate(float f0=0, float B=0, int df=0):f0(f0),B(B),df(df){} };
    list<Candidate, 2> candidates;
    float harmonicMax;
    float harmonicPower;
    float inharmonicity;
    float fPeak;
    /// Returns first partial (f1=f0*sqrt(1+B))
    /// \a fMin Minimum frequency for maximum peak selection (autocorrelation is still allowed to match lower pitches)
    /// \a fMax Maximum frequency for highest peak selection (maximum peak is still allowed to select higher pitches)
    float estimate(const ref<float>& signal, uint fMin, uint fMax) {
        candidates.clear();

        ref<float> halfcomplex = transform(signal);
        //float sum = 0;
        for(uint i: range(N/2)) {
            spectrum[i] = (sq(halfcomplex[i]) + sq(halfcomplex[N-1-i])) / N; // Converts to power spectrum
            //sum += spectrum[i];
        }
        //float scale = (N/2) / sum;
        //for(uint i: range(N/2)) spectrum[i] *= average; // Normalizes

        float maxPeak=0; uint F=0;
        for(uint i: range(fMin, N/2)) if(spectrum[i] > maxPeak) maxPeak=spectrum[i], F=i; // Selects maximum peak
        const uint nMin = ::max(F/fMax, (uint)ceil((F+2)*H*sqrt(2.)/(N/2)));
        const uint nMax = min(F/fMin, H);
        float maxEnergy = 0, bestF0=0, bestB = 0; float harmonicEnergy = 0;
        for(uint n0: range(nMin, nMax+1)) { // F rank hypothesis
            float rankMax=0, rankF0=0, rankB=0; int rankDf=0;
            for(int df: range(/*-1*/0,1)) {
                for(int b: range(F)) { // Inharmonicity hypothesis (negative?)
                    const float B = (float)b/(F*H*H);
                    const float F0 = (F+df)/(n0*sqrt(1+B*sq(n0))); // f0 under current rank and inharmonicity hypotheses
                    float energy=1;
                    for(uint n : range(1, H+1)) {
                        const uint f = round(F0*n*sqrt(1+B*sq(n)));
                        assert_(f<N/2);
                        energy += spectrum[f];
                    }
                    //candidates.insert(energy, Candidate{rankF0, B});
                    if(energy > rankMax) rankMax=energy, rankF0 = F0, rankB=B, rankDf=df;
                }
            }
            const float octaveThreshold = 1+1./16; // Threshold to avoid octave errors by matching exact same harmonics
            if(rankMax > octaveThreshold*maxEnergy) {
                maxEnergy=rankMax, bestF0=rankF0, bestB = rankB;
                candidates.insert(rankMax, Candidate{rankF0, rankB, rankDf});
            }
            assert_(rankMax > 0); harmonicEnergy += rankMax;
        }
        harmonicMax = maxEnergy;
        if(nMin<nMax+1) harmonicPower = harmonicEnergy / (nMax+1-nMin);
        inharmonicity = bestB;
        fPeak = F;
        return bestF0*sqrt(1+bestB);
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+3)%12]+str(key/12-2); }

// H(s) = (s^2 + 1) / (s^2 + s/Q + 1)
// Biquad notch filter
struct Notch {
    real frequency, bandwidth;
    float a1,a2,b0,b1,b2;
    Notch(real f, real bw) : frequency(f), bandwidth(bw) {
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
