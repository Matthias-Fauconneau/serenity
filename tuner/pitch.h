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
    buffer<float> spectrum {N/2};
    struct Candidate {
        float f0, B; int df; uint H; uint peakCount;
        Candidate(float f0=0, float B=0, int df=0, uint H=0, uint peakCount=0):
            f0(f0),B(B),df(df),H(H),peakCount(peakCount){} };
    list<Candidate, 3> candidates;
    float totalPower;
    float meanDensity;
    uint fPeak;
    float maxPeak;
    float limit;
    float harmonicPower;
    float inharmonicity;
    uint peakCount;
    int bestDf;
    const uint peakThreshold = exp2(8); //32; // from full scale
    const uint meanThreshold = exp2(23./8); //8; //32; // from full scale
    //const uint peakThresholdBelow = peakThreshold /*32*/; // from full scale (below fPeak)
    const int peakRadius = 0; //1; //2;
    /// Returns first partial (f1=f0*sqrt(1+B))
    /// \a fMin Minimum fundamental frequency for harmonic evaluation
    float estimate(const ref<float>& signal, const uint fMin, const uint fMax) {
        candidates.clear();

        ref<float> halfcomplex = transform(signal);
        float power=0;
        for(uint i: range(N/2)) {
            spectrum[i] = (sq(halfcomplex[i]) + sq(halfcomplex[N-1-i])) / N; // Converts amplitude to power spectrum density
            power += spectrum[i];
        }
        totalPower = power;
        float mean = power / (N/2);
        meanDensity = mean;

        float maxPeak=0; uint F=0;
        for(uint i: range(fMin, N/2)) if(spectrum[i] > maxPeak) maxPeak=spectrum[i], F=i; // Selects maximum peak
        limit = (maxPeak/peakThreshold+mean*meanThreshold)/2;
        //limit = max(maxPeak/peakThreshold,mean*meanThreshold);

        float bestPower = 0, bestF0 = 0, bestB = 0; uint bestN=1, bestPeakCount=0; float totalHarmonicEnergy = 0;
        const uint H = 21; //62
        for(uint n0: range(max(1u, F/fMax), min(H, F/fMin))) { // F rank hypothesis
            float rankPower=0, rankF0=0, rankB=0; int rankDf=0; uint rankH=0, rankPeakCount=0;
            for(int df: range(/*-1*/0,/*1*/0+1)) {
                const uint precision = 64; //64 2;//1 //8
                const float maxB = (exp2((float) F/n0 / (N/2))-1) * 1 / precision; //B~1/512
                for(int b: range(precision)) { // Inharmonicity hypothesis (Number of bins the highest harmonic may move)
                    const float B = maxB * b;
                    const float F0 = (F+df)/(n0*sqrt(1+B*sq(n0))); // f0 under current rank and inharmonicity hypotheses
                    float energy=0;
                    uint peakCount=0;
                    //for(uint n : range(1, H+1)) {
                    uint n=1; for(;n<=min(H, uint((N/2)*n0/F)); n++) {
                        const uint f = round(F0*n*sqrt(1+B*sq(n)));
                        assert_(f>0 && uint(round(F0*n*sqrt(1+B*sq(n))))+1 <= uint(round(F0*(n+1)*sqrt(1+B*sq(n+1)))), F, n0, fMin, F/fMin, F0, n, 1./B);
                        //if(f<fMax-1) energy += max(max(spectrum[f-1],spectrum[f]),spectrum[f+1]);
                        if(f<N/2-peakRadius) {
                            energy += spectrum[f];
                            assert_(f>=uint(peakRadius) && f<N/2-peakRadius, f);
                            assert_(n!=n0 || spectrum[f]==maxPeak);
                            for(int df: range(-peakRadius, peakRadius+1)) {
                                if(spectrum[f+df] > limit) { //*(n<n0?peakThresholdBelow:peakThreshold)*/) {
                                    //log(peakCount, n, f, df, f+df, spectrum[f+df]);
                                    peakCount++;
                                    break;
                                }
                            }
                        }
                        else break;
                    }
                    //candidates.insert(energy, Candidate{rankF0, B});
                    if(peakCount/**energy*/ > rankPeakCount/**rankPower*/) {
                        rankPower=energy, rankF0 = F0, rankB=B, rankDf=df, rankH=n-1, rankPeakCount=peakCount;
                    }
                }
            }
            //if(rankPower > (1+(float)n0/bestN/16)*bestPower) candidates.insert(rankPower, Candidate{rankF0, rankB, rankDf, rankH});
            //if((1-sq(1-1./n0)) * rankPower > (1-sq(1-1./bestN)) * bestPower) { // Threshold to avoid octave errors by matching exact same harmonics
            //if(rankPower > (1+(float)n0/bestN/64)*bestPower) {
            if(rankPeakCount >= 1 && rankPeakCount>=bestPeakCount && (rankPeakCount>bestPeakCount || n0==bestN+1))
                candidates.insert(rankPeakCount, Candidate{rankF0, rankB, rankDf, rankH, rankPeakCount});
            if( /*(1+bestN)**/rankPeakCount/**rankPower*/ > /*(1+n0)**/bestPeakCount/**bestPower*/) {
                bestPower=rankPower, bestF0=rankF0, bestB = rankB, bestPeakCount=rankPeakCount, bestN=n0;
            }
            assert_(rankPower > 0); totalHarmonicEnergy += rankPower;
        }
        fPeak = F;
        this->maxPeak = maxPeak;
        harmonicPower = bestPower;
        inharmonicity = bestB;
        peakCount = bestPeakCount;
        assert_(harmonicPower < totalPower, totalPower, harmonicPower, F, bestB, bestF0*sqrt(1+bestB));
        return bestF0*sqrt(1+bestB);
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+15)%12]+str(key/12-2); }

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
