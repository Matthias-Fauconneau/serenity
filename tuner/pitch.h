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

template<Type V, uint N> struct list { // Small sorted list
    static constexpr uint size = N;
    struct element : V { float key; element(float key=0, V value=V()):V(value),key(key){} } elements[N];
    void clear() { ::clear(elements, N); }
    void insert(float key, V value) {
        int i=0; while(i<N && elements[i].key<key) i++; i--;
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
    const uint H = 16; //9 // Maximum harmonic count for A7 (TODO: increase as needed for bass tones)
    buffer<float> spectrum {N/2};
    buffer<float> smooth {N/2};
    struct Candidate { float f0; float B; Candidate(float f0=0, float B=0):f0(f0),B(B){} };
    list<Candidate, 2> candidates;
    float harmonicMax;
    float harmonicPower;
    float inharmonicity;
    /// Returns first partial (f1=f0*sqrt(1+B))
    /// \a fMin Minimum frequency for maximum peak selection (autocorrelation is still allowed to match lower pitches)
    /// \a fMax Maximum frequency for highest peak selection (maximum peak is still allowed to select higher pitches)
    float estimate(const ref<float>& signal, uint fMin, uint fMax) {
        candidates.clear();

        ref<float> halfcomplex = transform(signal);
        float maxPeak=0; uint F=0;
        for(uint i: range(N/2)) spectrum[i] = (sq(halfcomplex[i]) + sq(halfcomplex[N-1-i])) / N; // Converts to intensity spectrum
        const uint smoothRadius = 3;
        for(uint i: range(smoothRadius, N/2-smoothRadius)) { // Smooth (FIXME: Gaussian)
            float sum = 0; for(int di: range(-smoothRadius,smoothRadius+1)) sum+=spectrum[i+di];
            smooth[i] = sum/(smoothRadius+1+smoothRadius);
        }
        swap(spectrum, smooth);
        const uint peakAccuracy = 2;
        for(uint i: range(smoothRadius+peakAccuracy, N/2)) if(spectrum[i] > maxPeak) maxPeak=spectrum[i], F=i; // Selects maximum peak
        const float Bmax = 1./512; //(fMin*H*H);
        const uint nMin = ::max((F+peakAccuracy+smoothRadius)/fMax, (uint)ceil(F*H*sqrt(1+Bmax*sq(H))/(N/2-1)));
        const uint nMax = min((F-peakAccuracy-smoothRadius)/fMin, H);
        float max = 0, bestF0=0, bestB = 0; float harmonicEnergy = 0;
        for(uint n0: range(nMin, nMax+1)) { // F rank hypothesis
            float rankMax=0, rankF0=0, rankB=0;
            for(int df: range(-peakAccuracy, peakAccuracy+1)) { // peak offset
                for(int b: range(F*H*H*Bmax+1)) { // Inharmonicity hypothesis (negative?)
                    const float B = (float)b/(F*H*H);
                    const float F0 = (F+df)/(n0*sqrt(1+B*sq(n0))); // f0 under current rank and inharmonicity hypotheses
                    float HPS=1; // Harmonic product (~ sum of logarithm of harmonic frequency powers) (vs direct sum: allows for missing peaks)
                    for(uint n : range(1, H+1)) {
                        const uint f = round(F0*n*sqrt(1+B*sq(n)));
                        assert_(n!=n0 || f==(F+df));
                        assert_(f>smoothRadius && f<N/2-smoothRadius, f, b, B, F0, n);
                        HPS += spectrum[f] * f; // Corrects spectrum power for accoustic attenuation (A~f^-1)
                    }
                    //candidates.insert(HPS, Candidate{rankF0, B});
                    if(HPS > rankMax) rankMax=HPS, rankF0 = F0, rankB=B;
                }
            }
            candidates.insert(rankMax, Candidate{rankF0, rankB});
            if(rankMax > max) max=rankMax, bestF0=rankF0, bestB = rankB;
            assert_(rankMax > 0); harmonicEnergy += rankMax; // Accumulating in the exponent would not be stable ?
        }
        harmonicMax = (max) / H;
        if(nMin<nMax+1) harmonicPower = (harmonicEnergy) / H / (nMax+1-nMin);
        inharmonicity = bestB;
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
