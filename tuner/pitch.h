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
    static constexpr uint capacity = N;
    uint size = 0;
    struct element : V { float key; element(float key=0, V&& value=V()):V(move(value)),key(key){} } elements[N];
    void clear() { for(size_t i: range(size)) elements[i]=element(); size=0; }
    void insert(float key, V&& value) {
        int i=0;
        while(i<size && elements[i].key<=key) i++;
        if(size<capacity) {
            for(int j=size-1; j>=i; j--) elements[j+1]=move(elements[j]); // Shifts right
            size++;
            elements[i] = element(key, move(value)); // Inserts new candidate
        } else {
            if(i<=0) return; // New candidate would be lower than current
            if(elements[i-1].key==key) return;
            for(uint j: range(i-1)) elements[j]=move(elements[j+1]); // Shifts left
            assert_(i>=1 && i<=size, i, size, N);
            elements[i-1] = element(key, move(value)); // Inserts new candidate
        }
    }
    void remove(V key) {
        for(int i=0; i<size; i++) {
            if((V)elements[i] == key) {
                for(uint j: range(i, size-1)) elements[j]=move(elements[j+1]); // Shifts left
                size--;
                return;
            }
        }
    }
    const element& operator[](uint i) const { assert(i<size); return elements[i]; }
    const element& last() const { return elements[size-1]; }
    const element* begin() const { return elements; }
    const element* end() const { return elements+size; }
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
    // Parameters
    const uint rate = 96000; // Discards 50Hz harmonics for absolute harmonic energy evaluation
    const uint fMin = 8, fMax = N/16; //440*exp2(3+2./12)*N/rate; // 15 ~ 6000 Hz
    const uint iterationCount = 4; // Number of least square iterations
    const float initialInharmonicity = 0; //1./cb(24); // Initial inharmonicity
    const float noiseThreshold = 2; //2;
    const float highPeakThreshold = 8;
    const uint medianError = 4; //3, 5, 7
    const uint maxHarmonicCount = 16; //18,27
    // Conditions for median F0 override
    const uint lastHarmonicRank = 39; // 32
    const uint lastHarmonicFrequency = 868; // 321
    const uint peakRank = 6; // 6
    const uint peakFrequency = 113; // 118
    const uint minNum = 26; // 35, 41, 47, 59, 82
    const uint minDen = 79; // 69, 79, 90, 171, 178
    // Conditions for F1 override
    const uint minHighPeak = 506; // 506-509
    const uint minHighPeakNum = 83; // 83, 111
    const uint minHighPeakDen = 640; //857

    struct Peak {
        uint f;
        bool operator ==(const Peak& o) const { return f == o.f; }
    };
    uint minF, maxF;
    list<Peak, 16> peaks;

    float harmonicEnergy=0;
    float F0=0, B=0; // F0.n+F0.B.n^2 fit of all harmonics (1st harmonic is F0.(1+B))
    buffer<float> filteredSpectrum {fMax}; // Filtered power spectrum
    uint F1, nHigh, medianF0;

    struct Candidate {
        float f0; float B; float energy; array<uint> peaks;
        Candidate(float f0=0, float B=0, float energy=0, array<uint>&& peaks={}):
            f0(f0),B(B),energy(energy),peaks(move(peaks)){}
    };
    list<Candidate, 2> candidates;

    /// Returns first partial (f1=f0*sqrt(1+B)~f0*(1+B))
    /// \a fMin Minimum fundamental frequency for harmonic evaluation
    float estimate() {
        {harmonicEnergy=0, F0=0, B=0, this->F1=0, this->nHigh=0;}
        candidates.clear();

        ref<float> spectrum = transform();
        for(uint i: range(0, fMax)) filteredSpectrum[i] = 0;

        peaks.clear();
        minF=fMax, maxF=fMin; uint last=0; uint F1=0; uint maxPeak=0;
        for(uint i: range(19, fMax-2)) {
            if(spectrum[i- 1] < spectrum[i] && spectrum[i] > spectrum[i+1] && spectrum[i] > noiseThreshold*periodPower) {
                if(spectrum[i-2]/2 < spectrum[i-1] && spectrum[i+1] > spectrum[i+2]/2) {
                    // Copies peaks / Filters non peaks
                    filteredSpectrum[i] = spectrum[i];
                    for(uint j=i-1; j>0 && spectrum[j+1]>spectrum[j] && j>i-3; j--) filteredSpectrum[j] = spectrum[j];
                    for(uint j=i+1; j<fMax && spectrum[j-1]>spectrum[j] && j<i+3; j++) filteredSpectrum[j] = spectrum[j];
                    // Records peaks
                    if(i<minF) minF=i; if(i>maxF) maxF=i;
                    if(i-fMin > last) { peaks.insert(spectrum[i], {i}); } // Records maximum peaks
                    else if(spectrum[i]>spectrum[last]) { // Overwrites lower peak
                        peaks.remove({last}); // Ensures only one is kept
                        peaks.insert(spectrum[i], {i});
                    } // else skips lower peak
                    last=i;
                    if(spectrum[i] > maxPeak) maxPeak=spectrum[i], F1=i;
                }
            }
        }
        uint highPeak = 0;
        for(Peak peak: peaks.slice(max<int>(0,peaks.size-6/*5*/)))
            if(peak.f > minHighPeak && spectrum[peak.f] > highPeakThreshold*periodPower) highPeak = peak.f;
        spectrum = filteredSpectrum; // Cleans spectrum
        if(!peaks.size) { harmonicEnergy=0; return 0; }
        array<uint> byFrequency(peaks.size);
        for(Peak peak: peaks) byFrequency.insertSorted(peak.f); // Insertion sorts by frequency
        array<uint> distance (peaks.size);
        {uint last=0; for(uint f: byFrequency) { distance << f-last; last=f; }} // Compute distances
        uint medianF0 = ::median(distance);
        // Corrects outlying fundamental estimate from median
        if(highPeak && highPeak/medianF0>26) {
            medianF0 = highPeak;
            for(const auto& peak: peaks.slice(max<int>(0,peaks.size-5))) {
                if(peak.f >= highPeak*minHighPeakNum/minHighPeakDen && peak.f >= minHighPeakNum && peak.f<medianF0) medianF0=peak.f;
            }
            F1=highPeak;
        } else {
            if(byFrequency.last()/medianF0>=lastHarmonicRank && byFrequency.last()>=lastHarmonicFrequency
                    && F1/medianF0>=peakRank && F1>=peakFrequency) {
                medianF0=F1;
                for(const auto& peak: peaks.slice(max<int>(0,peaks.size-6/*6*/))) {
                    if(peak.f >= F1*minNum/minDen && peak.f >= minNum && peak.f < medianF0) medianF0=peak.f;
                }
            }
        }

        uint nHigh = F1/max<int>(fMin, medianF0-medianError);
        this->F1=F1, this->medianF0 = medianF0, this->nHigh=nHigh;
        float bestEnergy = 0;
        for(uint n1: range(1, nHigh +1)) {
            float f0 = (float) F1 / (n1*(1 + initialInharmonicity*sq(n1))), f0B = f0*initialInharmonicity;
            float energy = 0; array<uint> peaks;
            for(uint t unused: range(1,iterationCount +1)) {
                peaks.clear(); energy = 0;
                // Least square optimization of linear fit: n.f0 = f[n] => argmin |Xb - y|^2 (X=n, b=f0, y=f[n]) <=> X'X b = X' y
                float n2=0, n3=0, n4=0, nf=0, n2f=0; // Least square X'X and X'y coefficients
                if(f0+f0B<fMin) f0=fMin;
                for(uint n=1; n<= maxHarmonicCount; n++) {
                    // Finds local maximum around harmonic frequencies predicted by last f0 estimation
                    int fn = round(f0*n + f0B*cb(n)); // Using Bn^2 instead of n*sqrt(1+Bn^2) in order to keep least square linear (assumes B<<1)
                    if(fn+fMin/2>=fMax) break;
                    uint f=fn; float peakEnergy = spectrum[f];
                    uint df=1;
                    for(; df<=fMin/8; df++) { // Fit nearest peak (FIXME: proportionnal to frequency)
                        if(spectrum[fn+df] > peakEnergy) peakEnergy=spectrum[fn+df], f=fn+df;
                        if(spectrum[fn-df] > peakEnergy) peakEnergy=spectrum[fn-df], f=fn-df;
                    }
                    peaks << f;
                    energy += peakEnergy;
                    for(; df<=fMin/2; df++) { // Fit nearest peak (FIXME: proportionnal to frequency)
                        if(spectrum[fn+df] > peakEnergy) peakEnergy=spectrum[fn+df], f=fn+df;
                        if(spectrum[fn-df] > peakEnergy) peakEnergy=spectrum[fn-df], f=fn-df;
                    }
                    float w = peakEnergy;
                    nf  += w * n * f;
                    n2 += w * n * n;
                    n2f+= w * cb(n) * f;
                    n3 += w * cb(n) * n;
                    n4 += w * cb(n) * cb(n);
                }
                // Solves X'X b = X'y
                float a=n2, b=n3, c=n3, d=n4;
                float det = a*d-b*c;
                if(det) {
                    f0B = (-c*nf + a*n2f) / det;
                    if(f0B>0) f0 = (d*nf - b*n2f) / det;
                    else if(n2) f0 = nf/n2, f0B=0;
                } else if(n2) f0 = nf/n2, f0B=0;
            }
            if(highPeak && f0 >= highPeak-1) energy += periodPower; // High peak boost
            if(f0) candidates.insert(energy, Candidate(f0, f0B/f0, energy, copy(peaks)));
            if(energy > bestEnergy) {
                bestEnergy = energy;
                this->F0 = f0;
                this->B = f0B/f0;
            }
        }
        harmonicEnergy = bestEnergy;
        if(highPeak && this->F0 >= highPeak-1) harmonicEnergy = periodEnergy ;
        return this->F0*(1+this->B);
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+2*12+3)%12]+str(key/12-2); }
