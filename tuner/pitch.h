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
    buffer<float> spectrum {N/2}; // Power spectrum
    float periodEnergy; //  Spectral energy of the last period
    float periodPower; // Mean spectral energy density over the last period
    /// Executes an FFT transform and computes power spectrum
    mref<float> transform() {
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

generic uint partition(const mref<T>& at, size_t left, size_t right, size_t pivotIndex) {
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
generic T quickselect(const mref<T>& at, size_t left, size_t right, size_t k) {
    for(;;) {
        size_t pivotIndex = partition(at, left, right, (left + right)/2);
        int pivotDist = pivotIndex - left + 1;
        if(pivotDist == k) return at[pivotIndex];
        else if(k < pivotDist) right = pivotIndex - 1;
        else { k -= pivotDist; left = pivotIndex + 1; }
    }
}
generic T median(const mref<T>& at) { if(at.size==1) return at[0u]; return quickselect(at, 0, at.size-1, at.size/2); }
generic T median(const ref<T>& at) { T buffer[at.size]; rawCopy(buffer,at.data,at.size); return median(mref<T>(buffer,at.size)); }

struct PitchEstimator : FFT {
    using FFT::FFT;
    const uint rate = 96000; // Discards 50Hz harmonics for absolute harmonic energy evaluation
    float harmonicEnergy=0;
    float F0=0, B=0; // F0.n+F0.B.n^2 fit of all harmonics (1st harmonic is F0.(1+B))
    buffer<float> filteredSpectrum {N/2}; // Filtered power spectrum
    uint medianF0, F1, nLow, nHigh;

    struct Candidate {
        float f0; float B; float energy; array<uint> peaks, peaksLS;
        Candidate(float f0=0, float B=0, float energy=0, array<uint>&& peaks={}, array<uint>&& peaksLS={}):
            f0(f0),B(B),energy(energy),peaks(move(peaks)),peaksLS(move(peaksLS)){}
    };
    list<Candidate, 2> candidates;

    /// Returns first partial (f1=f0*sqrt(1+B)~f0*(1+B))
    /// \a fMin Minimum fundamental frequency for harmonic evaluation
    float estimate() {
        candidates.clear();

        ref<float> spectrum = transform();
        for(uint i: range(0, N/2)) filteredSpectrum[i] = 0;

        struct Peak { uint f; };
        list<Peak, 3> maxPeaks;
        uint distance[512]; uint last=0, index=0;
        for(uint i: range(8, N/4)) {
            if(spectrum[i- 1] < spectrum[i] && spectrum[i] > spectrum[i+1]) {
                // Copies peaks / Filters non peaks
                float w = abs((int)1*50*N/rate-i) < 2 ? 1./8 : 1; // Attenuates 50Hz
                filteredSpectrum[i] = w*spectrum[i];
                for(uint j=i-1; j>0 && spectrum[j+1]>spectrum[j]; j--) filteredSpectrum[j] = w*spectrum[j];
                for(uint j=i+1; j<N/2 && spectrum[j-1]>spectrum[j]; j++) filteredSpectrum[j] = w*spectrum[j];

                if(spectrum[i] > 2*periodPower && spectrum[i-2] < spectrum[i-1] && spectrum[i+1] > spectrum[i+2]) {
                    // Records distance between peaks
                    if(i-last > 6) { assert_(index < 512); distance[index++] = i-last; last=i; }
                    else if(spectrum[i]>spectrum[last] && index) {  distance[index-1] += i-last; last=i; } // Overwrites lower peak
                    // else skips lower peak
                    if(i>17) maxPeaks.insert(spectrum[i], {i}); // Records maximum peaks
                }
            }
        }
        spectrum = filteredSpectrum; // Cleans spectrum
        uint F1=maxPeaks.last().f;
        uint medianF0 = index > 6 ? ::median(mref<uint>(distance,index)) : F1;  // "0th" order estimation of first partial (f0)
        this->medianF0 = medianF0;
        for(const auto& peak: maxPeaks) if(peak.f < F1 && peak.f /medianF0 > 9) F1=peak.f; // Uses lowest maximum peak (avoids inharmonicity bias)
        uint nLow = F1/medianF0;
        uint nHigh = F1/(medianF0-4);
        this->nLow=nLow, this->nHigh=nHigh, this->F1=F1;
        float bestEnergy = 0, bestMerit = 0;
        for(uint n1: range(nLow>1 ? nLow-1 : 1,  nHigh +1)) {
            if(n1==nLow-1) n1=1; // Always evaluate n1=1
            float f0 = (float) F1 / n1, f0B = f0/cb(22), energy = 0;//, filteredEnergy=0;
            array<uint> peaks, peaksLS;
            for(uint t: range(1,3)) {
                peaks.clear(); peaksLS.clear(); energy = 0;//, filteredEnergy=0;
                // Least square optimization of linear fit: n.f0 = f[n] => argmin |Xb - y|^2 (X=n, b=f0, y=f[n]) <=> X'X b = X' y
                float n2=0, n3=0, n4=0, nf=0, n2f=0; // Least square X'X and X'y coefficients
                const int fMin = 5;
                if(f0+f0B<fMin) f0=fMin;
                for(uint n=1;; n++) { // Every candidates stops at same peak
                    // Finds local maximum around harmonic frequencies predicted by last f0 estimation
                    int fn = round(f0*n + f0B*cb(n)); // Using Bn^2 instead of n*sqrt(1+Bn^2) in order to keep least square linear (assumes B<<1)
                    if(fn+fMin>=int(N/2)) break;
                    uint f=fn; float peakEnergy = spectrum[f];
                    peaks << f;
                    int df=1;
                    energy += peakEnergy;
                    for(; df<=fMin; df++) { // Fit nearest peak
                        if(spectrum[fn+df] > peakEnergy) peakEnergy=spectrum[fn+df], f=fn+df;
                        if(spectrum[fn-df] > peakEnergy) peakEnergy=spectrum[fn-df], f=fn-df;
                    }
                    peaksLS << f;
                    if(n<=t*19) { // Refines f0 with a weighted least square fit
                        float w = peakEnergy;
                        n2 += w * n*n;
                        nf += w * n * f;
                        n3 += w * n*cb(n);
                        n4 += w * cb(n)*cb(n);
                        n2f += w * cb(n) * f;
                    }
                }
                // Solves X'X b = X'y
                float a=n2, b=n3, c=n3, d=n4;
                float det = a*d-b*c;
                assert_(det);
                f0B = (-c*nf + a*n2f) / det;
                if(f0B>0) f0 = (d*nf - b*n2f) / det;
                else f0 = nf/n2, f0B=0;
            }
            float merit = energy; // / (10554+peaks.size); // Keeps higher octaves
            candidates.insert(merit, Candidate(f0, f0B/f0, energy, copy(peaks), copy(peaksLS)));
            if(merit > bestMerit) {
                bestMerit = merit;
                bestEnergy = energy;
                this->F0 = f0;
                this->B = f0B/f0;
            }
        }
        harmonicEnergy = bestEnergy;
        return this->F0*(1+this->B);
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+2*12+3)%12]+str(key/12-2); }

