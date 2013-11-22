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
    /// Setups an FFT of size N (with a flat top window).
    FFT(uint N) : N(N) { for(uint i: range(N)) { const real z = 2*PI*i/N; window[i] = 1 - 1.90796*cos(z) + 1.07349*cos(2*z) - 0.18199*cos(3*z); }}
    /// Windows signal and executes an FFT transform
    ref<float> transform(const ref<float>& signal={}) {
        assert(N == signal.size);
        for(uint i: range(N)) windowed[i] = window[i]*signal[i]; // Multiplies window
        fftwf_execute(fftw); // Transforms (FIXME: use execute_r2r and free windowed/transform buffers between runs)
        return halfcomplex;
    }

    buffer<float> spectrum {N/2}; // Power spectrum
    float periodEnergy; // {Signal | Spectral} energy of the last period
    float periodPower; // Mean { power (E/T) | spectral energy density (E/F-1) } over the last period
    /// Transforms signal and computes power spectrum
    mref<float> powerSpectrum(const ref<float>& signal={}) {
        ref<float> halfcomplex = transform(signal);
        float energy=0;
        for(uint i: range(N/2)) {
            spectrum[i] = (sq(halfcomplex[i]) + sq(halfcomplex[N-1-i])) / N; // Converts amplitude to power spectrum density
            energy += spectrum[i];
        }
        periodEnergy = energy;
        periodPower = energy / (N/2);
        return spectrum;
    }
};

template<Type V, uint N> struct list { // Small sorted list
    static constexpr uint size = N;
    struct element : V { float key; element(float key=0, V&& value=V()):V(move(value)),key(key){} } elements[N];
    void clear() { for(size_t i: range(size)) elements[i]=element(); }
    void insert(float key, V&& value) {
        int i=0; while(i<N && elements[i].key<=key) i++; i--;
        if(key==elements[0].key) return;
        if(i<0) return; // New candidate would be lower than current
        for(uint j: range(i)) elements[j]=move(elements[j+1]); // Shifts left
        elements[i] = element(key, move(value)); // Inserts new candidate
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
generic T median(const mref<T>& at) { /*assert(at.size%2==1);*/ return quickselect(at, 0, at.size-1, at.size/2); }
generic T median(const ref<T>& at) { T buffer[at.size]; rawCopy(buffer,at.data,at.size); return median(mref<T>(buffer,at.size)); }

generic void quicksort(mref<T>& at, int left, int right) {
    if(left < right) { // If the list has 2 or more items
        int pivotIndex = partition(at, left, right, (left + right)/2);
        if(pivotIndex) quicksort(at, left, pivotIndex-1);
        quicksort(at, pivotIndex+1, right);
    }
}
/// Quicksorts the array in-place
generic void sort(mref<T>& at) { if(at.size) quicksort(at, 0, at.size-1); }

struct PitchEstimator : FFT {
    using FFT::FFT;
    float totalPeriodsEnergy = 0;
    uint periodCount = 0;
    float meanPeriodEnergy;

    uint firstPeakFrequency;
    float medianF0;
    uint lastPeak;
    float harmonicEnergy;
    float B;

    struct Candidate {
        float f0; float B; float energy; uint peakCount; float HPS; array<uint> peaks0, peaks, peaksLS;
        Candidate(float f0=0, float B=0, float energy=0, uint peakCount=0, float HPS=0,
                  array<uint>&& peaks0={}, array<uint>&& peaks={}, array<uint>&& peaksLS={}):
            f0(f0),B(B),energy(energy),peakCount(peakCount),HPS(HPS),peaks0(move(peaks0)),peaks(move(peaks)),peaksLS(move(peaksLS)){}
    };
    list<Candidate, 5> candidates;


    /// Returns first partial (f1=f0*sqrt(1+B)~f0*(1+B))
    /// \a fMin Minimum fundamental frequency for harmonic evaluation
    float estimate(const ref<float>& signal, const uint fMin, const uint fMax unused) {

        ref<float> spectrum = powerSpectrum(signal);
        // Accumulates evaluation of each period energy to use as a reference for an absolute threshold
        totalPeriodsEnergy += periodEnergy; periodCount++;
        meanPeriodEnergy = totalPeriodsEnergy/periodCount;

        float maximum=0; int F1=0; // F1: Maximum peak
        uint distance[512]; uint last=0, index=0;
        for(uint i: range(fMin, N/2-3)) {
            if(spectrum[i] > 2*periodPower
                    && spectrum[i- 2] < spectrum[i-  1] && spectrum[i- 1] < spectrum[i] && spectrum[i-  2] < spectrum[i]
                    && spectrum[i+2] < spectrum[i+1] && spectrum[i+1] < spectrum[i] && spectrum[i+2] < spectrum[i]) {
                if(i-last > fMin) { assert_(index < 512); distance[index++] = i-last; last=i; }
                else if(spectrum[i]>spectrum[last] && index) {  distance[index-1] += i-last; last=i; } // Overwrites lower peak //else skips lower peak
                if(spectrum[i] > maximum) maximum=spectrum[i], /*F2=F1,*/ F1=i; // Selects maximum peak and keeps second (lower than first)
            }
        }
        if(!index) return 0;
        firstPeakFrequency = F1;

        float F0 = ::median(mref<uint>(distance,index));  // "0th" order estimation of first partial (f0)
        medianF0 = F0;

        lastPeak=0;
        candidates.clear();
        float bestEnergy = 0; uint bestPeakCount=0;
        uint nLow = max(1,int(F1/F0)), nHigh = max(nLow+2, uint(round(F1/F0*4/3))); //5/4))); //F1/F0*5/4)); //6/5));
        float f0Low = (float) F1 / nHigh;
        log(F1/F0, nHigh);
        for(uint n1: range(nLow,  nHigh)) {
            float f0 = (float) F1 / n1;
            float f0B = 0;
            float totalEnergy = 0; uint peakCount=0; float HPS=0;
            array<uint> peaks0, peaks, peaksLS;
            for(uint t unused: range(5)) { // until convergence ?
                peaks0.clear(); peaks.clear(); peaksLS.clear(); totalEnergy = 0; peakCount=0; HPS=0; uint productCount=0;
                // Least square optimization of linear fit: n.f0 = f[n] => argmin |Xb - y|^2 (X=n, b=f0, y=f[n]) <=> X'X b = X' y
                float n2=0, n4=0, n6=0, nf=0, n3f=0; // Least square X'X and X'y coefficients
                for(uint n=1; n<=(31/*28*//nLow)*n1; n++) {
                    // Finds local maximum around harmonic frequencies predicted by last f0 estimation
#if 0
                    uint expectedF = f0*n*(1+B*n*n); // Using (1+Bn^2) instead of sqrt(1+Bn^2) in order to keep least square linear (assumes B<<1)
#else
                    uint expectedF = f0*n + f0B*n*n; // Using (1+Bn^2) instead of sqrt(1+Bn^2) in order to keep least square linear (assumes B<<1)
#endif
                    //if(expectedF>fMax) break;
                    if(expectedF>N/2) break;
                    uint localF = expectedF; float peakEnergy = spectrum[localF]; // Weighted by 1/(1+distance) //FIXME: integrate energy ?
                    //for(uint df=1; df < /*(uint)(f0Low/2)*//*f0Low*/f0/4; df++) { // Avoids solution only fitting the strongest peaks extremely well
                    uint df=1;
#if 0
                    for(; df < f0Low/8; df++) { // Avoids solution only fitting the strongest peaks extremely well
                        assert_(int(expectedF-df)>=0 && expectedF+df<N/2);
                        float w = 1; //1./(1+df); //1./(1+df) - 1./(1+maxDistance);
                        // Only search below to avoid interval shift ?
                        if(w*spectrum[expectedF-df] > peakEnergy) peakEnergy=w*spectrum[expectedF-df], localF=expectedF-df;
                        if(w*spectrum[expectedF+df] > peakEnergy) peakEnergy=w*spectrum[expectedF+df], localF=expectedF+df;
                    }
#endif
                    peaks0 << expectedF;
                    //if(peakEnergy > 2*periodPower) {
                    //if(peakEnergy > periodPower/8) {
                        totalEnergy += peakEnergy;
                        //if(spectrum[expectedF]>periodPower) lastHarmonicRank=max(lastHarmonicRank, n);
                        peakCount += peakEnergy > 3*periodPower; // FIXME: explicit parameter
                        assert_(peakEnergy>0);
                        if(peakEnergy > periodPower*2) { HPS += log2(peakEnergy); productCount++; }
                        peaks << localF;
                        if(peakEnergy > periodPower*2)
                            lastPeak = max(lastPeak, localF);
                        // Refines f0 with a weighted least square fit
                        for(; df < f0Low/3; df++) { // Search higher frequencies further to fit any inharmonicity
                            assert_(int(expectedF-df)>=0 && expectedF+df<N/2);
                            float w = 1; //1./(1+df); //1./(1+df) - 1./(1+maxDistance);
                            // Only search below to avoid interval shift ?
                            //if(w*spectrum[expectedF-df] > peakEnergy) peakEnergy=w*spectrum[expectedF-df], localF=expectedF-df;
                            if(w*spectrum[expectedF+df] > peakEnergy) peakEnergy=w*spectrum[expectedF+df], localF=expectedF+df;
                        }
                        peaksLS << localF;
                        float w = peakEnergy / n; //log2(peakEnergy); //./(1+n); //weightedEnergyDensity; // Weights by energy density, distance from integer harmonic //, rank
                        n2 += w * n*n;
                        nf += w * n * localF;
#if 0
                        n4 += w * n*cb(n);
                        n6 += w * cb(n)*cb(n);
                        n3f += w * cb(n) * localF;
#else
                        n4 += w * n*sq(n);
                        n6 += w * sq(n)*sq(n);
                        n3f += w * sq(n) * localF;
#endif
                }
                assert_(n2,nLow);
                // Solves X'X b = X'y
                real a=n2, b=n4, c=n4, d=n6;
                real det = a*d-b*c;
                f0 = (d*nf - b*n3f) / det;
                f0B = (-c*nf + a*n3f) / det;
                if(f0B<0)
                    f0 = nf/n2, f0B=0;
            }
            const float energyWeight = 16;
            candidates.insert(energyWeight*totalEnergy+peakCount, Candidate(f0, f0B/f0, totalEnergy, peakCount, HPS, copy(peaks0), copy(peaks), copy(peaksLS)));
            if(energyWeight*totalEnergy+peakCount > energyWeight*bestEnergy+bestPeakCount) {
                bestPeakCount = peakCount;
                bestEnergy = totalEnergy;
                F0 = f0;
                this->B = f0B/f0;
            }
        }
        harmonicEnergy = bestEnergy;
        return F0;
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+15)%12]+str(key/12-2); }

