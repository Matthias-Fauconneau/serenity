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
    float periodEnergy; // Spectral energy of the last period (same as signal energy by Parseval)
    mref<float> powerSpectrum(const ref<float>& signal={}) {
        ref<float> halfcomplex = transform(signal);
        float periodEnergy=0;
        for(uint i: range(N/2)) {
            spectrum[i] = (sq(halfcomplex[i]) + sq(halfcomplex[N-1-i])) / N; // Converts amplitude to power spectrum density
            periodEnergy += spectrum[i];
        }
        this->periodEnergy = periodEnergy;
        return spectrum;
    }
};

template<Type V, uint N> struct list { // Small sorted list
    static constexpr uint size = N;
    struct element : V { float key; element(float key=0, V value=V()):V(value),key(key){} } elements[N];
    void clear() { ::clear(elements, N); }
    void insert(float key, V value) {
        int i=0; while(i<N && elements[i].key<=key) i++; i--;
        //if(key==elements[0].key) return;
        if(i<0) return; // New candidate would be lower than current
        for(uint j: range(i)) elements[j]=elements[j+1]; // Shifts left
        elements[i] = element(key, value); // Inserts new candidate
    }
    const element& operator[](uint i) const { assert(i<N); return elements[i]; }
    const element& last() const { return elements[N-1]; }
    const element* begin() const { return elements; }
    const element* end() const { return elements+N; }
    ref<element> slice(size_t pos) const { assert(pos<=size); return ref<element>(elements+pos,size-pos); }
};

generic uint partition(const mref<T>& at, int left, int right, int pivotIndex) {
    swap(at[pivotIndex], at[right]);
    const T& pivot = at[right];
    uint storeIndex = left;
    for(uint i: range(left,right)) {
        if(at[i] < pivot) {
            swap(at[i], at[storeIndex]);
            storeIndex++;
        }
    }
    swap(at[storeIndex], at[right]);
    return storeIndex;
}
generic T quickselect(const mref<T>& at, int left, int right, int k) {
    for(;;) {
        int pivotIndex = partition(at, left, right, (left + right)/2);
        int pivotDist = pivotIndex - left + 1;
        if(pivotDist == k) return at[pivotIndex];
        else if(k < pivotDist) right = pivotIndex - 1;
        else { k -= pivotDist; left = pivotIndex + 1; }
    }
}
generic T median(const mref<T>& at) { assert(at.size%2==1); return quickselect(at, 0, at.size-1, at.size/2); }
generic T median(const ref<T>& at) { T buffer[at.size]; rawCopy(buffer,at.data,at.size); return median(mref<T>(buffer,at.size)); }

struct PitchEstimator : FFT {
    using FFT::FFT;
    struct Candidate {
        float f0, B; uint H; uint peakCount;
        Candidate(float f0=0, float B=0, uint H=0, uint peakCount=0):
            f0(f0),B(B),H(H),peakCount(peakCount){} };
    list<Candidate, 8> candidates;
    float totalPeriodsEnergy = 0;
    uint periodCount = 0;
    float meanPeriodEnergy;
    float mean;
    float median;
    uint fPeak;
    float maxPeak;
    float noiseThreshold;
    float harmonicPower;
    float inharmonicity;
    uint peakCount;
    uint lastHarmonicRank;
    const float peakThreshold = exp2(-7);
    const float medianThreshold = exp2(7); //6
    const float meanThreshold = exp2(1);
    const uint maximumHarmonicRank = 23; //21, 47, 52, 62
    //static const int Pa=15,Ha=16, Pb=12,Hb=12;
    //static const int Pa=11,Ha=13, Pb=8,Hb=8;
    static const int Pa=12,Ha=16, Pb=16,Hb=22;
    static constexpr float peakCountRatioTradeoff = (float) (Pa * Hb - Pb * Ha) / (Pb - Pa); // Ma=Mb <=> C = (Pa * Hb - Pb * Ha) / (Pb - Pa)
    static_assert(peakCountRatioTradeoff>=0,"");
    PitchEstimator(uint N) : FFT(N) {
        //{int Pa=5,Ha=6, Pb=4,Hb=4; log(Pa,Ha,Pb,Hb, (float) (Pa * Hb - Pb * Ha) / (Pb - Pa));}
        //{int Pa=5,Ha=7, Pb=4,Hb=5; log(Pa,Ha,Pb,Hb, (float) (Pa * Hb - Pb * Ha) / (Pb - Pa));}
    }
    /// Returns first partial (f1=f0*sqrt(1+B))
    /// \a fMin Minimum fundamental frequency for harmonic evaluation
    float estimate(const ref<float>& signal, const uint fMin, const uint fMax) {
        candidates.clear();

        ref<float> spectrum = powerSpectrum(signal);
        totalPeriodsEnergy += periodEnergy; periodCount++;
        meanPeriodEnergy = totalPeriodsEnergy/periodCount;

        mean = periodEnergy / (N/2); // Mean spectral energy density
        median = ::median(spectrum); // Median spectral energy density
        //noiseThreshold = max(peakThreshold*maxPeak, max(mean*meanThreshold,median*medianThreshold));
        //noiseThreshold = max(mean*meanThreshold,median*medianThreshold);
        noiseThreshold = median*medianThreshold;

        float maxPeak=0; uint F=0;
        for(uint i: range(fMin, N/2)) if(spectrum[i] > maxPeak) maxPeak=spectrum[i], F=i; // Selects maximum peak

        float bestPower = 0, bestF0 = 0, bestB = 0; uint bestH=maximumHarmonicRank, bestPeakCount=0; //float totalHarmonicEnergy = 0;
        for(uint n0: range(max(1u, F/fMax), min(maximumHarmonicRank, F/fMin))) { // F rank hypothesis
            float rankPower=0, rankF0=0, rankB=0; uint rankH=maximumHarmonicRank, rankPeakCount=0;
            for(int peakOffsetIndex: range(0,2)) { // Optimizes using peak offset
                int df = peakOffsetIndex ? ((peakOffsetIndex-1)%2?1:-1) * (peakOffsetIndex-1)/2 : 0;
                const uint precision = 2; //8; //8; // 8/1
                const float maxB = 1 * (exp2((float) F/n0 / (N/2))-1) / precision; //1/2 (B~1/512)
                //const float maxB = 1./1024;
                for(int b: range(precision)) { // Optimizes using inharmonicity
                    const float B = maxB * b;
                    const float F0 = (F+df)/(n0*sqrt(1+B*sq(n0))); // 0th partial under current rank and inharmonicity hypotheses
                    float energy=0;
                    uint bestPeakCount=0, bestH=maximumHarmonicRank;
                    {
                        uint peakCount=0; uint lastH=0;
                        for(uint n: range(1, maximumHarmonicRank+1)) {
                            const uint f = round(F0*n*sqrt(1+B*sq(n)));
                            assert_(f>0 && uint(round(F0*n*sqrt(1+B*sq(n))))+1 <= uint(round(F0*(n+1)*sqrt(1+B*sq(n+1)))));
                            if(f>=N/2) break;
                            energy += spectrum[f];
                            assert_(f<N/2 && (n!=n0 || spectrum[f-df]==maxPeak));
                            if(spectrum[f] > noiseThreshold) {
                                lastH = n; peakCount++;
                                if((bestH+peakCountRatioTradeoff)*peakCount >= (lastH+peakCountRatioTradeoff)*bestPeakCount) { // Prevents large H increase
                                    bestH=lastH, bestPeakCount=peakCount;
                                }
                            }
                        }
                    }
                    if( bestPeakCount>rankPeakCount &&
                            (rankH+peakCountRatioTradeoff)*bestPeakCount > (bestH+peakCountRatioTradeoff)*rankPeakCount) {
                        rankPower=energy, rankF0 = F0+df, rankB=B, rankH=bestH, rankPeakCount=bestPeakCount;
                        candidates.insert((float)rankPeakCount/(rankH+peakCountRatioTradeoff), Candidate{rankF0, rankB, rankH, rankPeakCount});
                    }
                }
            }
            if((bestH+peakCountRatioTradeoff)*rankPeakCount > (rankH+peakCountRatioTradeoff)*bestPeakCount
                    || ((bestH+peakCountRatioTradeoff)*rankPeakCount >= (rankH+peakCountRatioTradeoff)*bestPeakCount &&
                        rankPeakCount>=bestPeakCount)) {
                bestPower=rankPower, bestF0=rankF0, bestB = rankB, bestPeakCount=rankPeakCount, bestH=rankH;
            }
            /*if(rankPeakCount>=bestPeakCount)
                candidates.insert((float)rankPeakCount/(rankH+peakCountRatioTradeoff), Candidate{rankF0, rankB, rankH, rankPeakCount});*/
            //totalHarmonicEnergy += rankPower;
        }
        fPeak = F;
        this->maxPeak = maxPeak;
        harmonicPower = bestPower;
        inharmonicity = bestB;
        peakCount = bestPeakCount;
        lastHarmonicRank = bestH;
        return bestF0*sqrt(1+bestB);
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+15)%12]+str(key/12-2); }

