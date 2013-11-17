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
        //assert(isPowerOfTwo(N));
        for(uint i: range(N)) { const real z = 2*PI*i/N; window[i] = 1 - 1.90796*cos(z) + 1.07349*cos(2*z) - 0.18199*cos(3*z); }
    }
    ref<float> transform(const ref<float>& signal={}) {
        assert(N == signal.size);
        for(uint i: range(N)) windowed[i] = window[i]*signal[i]; // Multiplies window
        fftwf_execute(fftw); // Transforms (FIXME: use execute_r2r and free windowed/transform buffers between runs)
        return halfcomplex;
    }
    buffer<float> spectrum {N/2}; // Power spectrum
    float power; // Spectral energy (same as signal energy by Parseval)
    mref<float> powerSpectrum(const ref<float>& signal={}) {
        ref<float> halfcomplex = transform(signal);
        float power=0;
        for(uint i: range(N/2)) {
            spectrum[i] = (sq(halfcomplex[i]) + sq(halfcomplex[N-1-i])) / N; // Converts amplitude to power spectrum density
            power += spectrum[i];
        }
        this->power = power;
        return spectrum;
    }
};

template<Type V, uint N> struct list { // Small sorted list
    static constexpr uint size = N;
    struct element : V { float key; element(float key=0, V value=V()):V(value),key(key){} } elements[N];
    void clear() { ::clear(elements, N); }
    void insert(float key, V value) {
        int i=0; while(i<N && elements[i].key<=key) i++; i--;
        //assert_(key!=elements[0].key);
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
    struct Candidate {
        float f0, B; uint H; uint peakCount;
        Candidate(float f0=0, float B=0, uint H=0, uint peakCount=0):
            f0(f0),B(B),H(H),peakCount(peakCount){} };
    list<Candidate, 2> candidates;
    float mean;
    uint fPeak;
    float maxPeak;
    float limit;
    float harmonicPower;
    float inharmonicity;
    uint peakCount;
    int bestDf;
    const float peakThreshold = exp2(8); // from full scale
    const float meanThreshold = exp2(1./2); //1 //2 //3
    //const int peakRadius = 0;
    const uint H = 32; //27; //52; //62
    //const uint minPeakCount = 6;
    // Balances merit M = P/(H+C) to select more peaks with lower ratio
    //const uint Pa=9, Ha=9, Pb = 16, Hb=18; // Selects lower octave even when missing peaks
    const uint Ha=9, Pa=Ha, Hb=2*Pa, Pb = 2*Ha-2; // Selects lower octave even when missing 2 peaks from 18
    const uint peakCountRatioTradeoff = (Pa * Hb - Pb * Ha) / (Pb - Pa); // Ma=Mb <=> C = (Pa * Hb - Pb * Ha) / (Pb - Pa)
    /// Returns first partial (f1=f0*sqrt(1+B))
    /// \a fMin Minimum fundamental frequency for harmonic evaluation
    float estimate(const ref<float>& signal, const uint fMin, const uint fMax) {
        candidates.clear();

        ref<float> spectrum = powerSpectrum(signal);
        mean = power / (N/2);

        float maxPeak=0; uint F=0;
        for(uint i: range(fMin, N/2)) if(spectrum[i] > maxPeak) maxPeak=spectrum[i], F=i; // Selects maximum peak
        limit = min(maxPeak/peakThreshold,mean*meanThreshold);

        float bestPower = 0, bestF0 = 0, bestB = 0; uint bestH=H, bestPeakCount=0; float totalHarmonicEnergy = 0;
        for(uint n0: range(max(1u, F/fMax), min(H, F/fMin))) { // F rank hypothesis
            float rankPower=0, rankF0=0, rankB=0; uint rankH=H, rankPeakCount=0;
            const uint precision = 4; //16; //32; //64;
            const float maxB = (exp2((float) F/n0 / (N/2))-1) * 1 / precision; //B~1/512
            for(int b: range(precision)) { // Inharmonicity hypothesis (Number of bins the highest harmonic may move)
                const float B = maxB * b;
                const float F0 = F/(n0*sqrt(1+B*sq(n0))); // f0 under current rank and inharmonicity hypotheses
                float energy=0;
                uint peakCount=0;
                uint lastH=0;
                uint n=1; for(;n<=min(H, uint((N/2)*n0/F)); n++) {
                    const uint f = round(F0*n*sqrt(1+B*sq(n)));
                    assert_(f>0 && uint(round(F0*n*sqrt(1+B*sq(n))))+1 <= uint(round(F0*(n+1)*sqrt(1+B*sq(n+1)))));
                    if(f<N/2) {
                        energy += spectrum[f];
                        assert_(f<N/2 && (n!=n0 || spectrum[f]==maxPeak));
                        if(spectrum[f] > limit) { lastH = n; peakCount++; }
                    }
                    else break;
                }
                //if(peakCount>=minPeakCount && rankH*peakCount >= lastH*rankPeakCount) {
                if((rankH+peakCountRatioTradeoff)*peakCount >= (lastH+peakCountRatioTradeoff)*rankPeakCount) {
                    rankPower=energy, rankF0 = F0, rankB=B, rankH=lastH, rankPeakCount=peakCount;
                }
            }
            candidates.insert((float)rankPeakCount/(rankH+peakCountRatioTradeoff), Candidate{rankF0, rankB, rankH, rankPeakCount});
            if((bestH+peakCountRatioTradeoff)*rankPeakCount >= (rankH+peakCountRatioTradeoff)*bestPeakCount) {
                bestPower=rankPower, bestF0=rankF0, bestB = rankB, bestPeakCount=rankPeakCount, bestH=rankH;
            }
            totalHarmonicEnergy += rankPower;
        }
        fPeak = F;
        this->maxPeak = maxPeak;
        harmonicPower = bestPower;
        inharmonicity = bestB;
        peakCount = bestPeakCount;
        return bestF0*sqrt(1+bestB);
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+15)%12]+str(key/12-2); }

