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

    float harmonicEnergy;

#if 1
    array<float> leastSquareF0; array<array<uint>> peaks0, peaks;
    float bestF0; array<uint> bestPeaks0, bestPeaks;
    const ref<uint> maxRanks = {1,1};
    uint lastHarmonicRank;
    uint lastPeak;

    struct Candidate {
        float f0; float energy; uint peakCount; float HPS; array<uint> peaks0, peaks, peaksLS;
        Candidate(float f0=0, float energy=0, uint peakCount=0, float HPS=0, array<uint>&& peaks0={}, array<uint>&& peaks={}, array<uint>&& peaksLS={}):
            f0(f0),energy(energy),peakCount(peakCount),HPS(HPS),peaks0(move(peaks0)),peaks(move(peaks)),peaksLS(move(peaksLS)){}
    };
    list<Candidate, 5> candidates;
#else
    struct Candidate {
        float f0, B; uint H; uint peakCount;
        Candidate(float f0=0, float B=0, uint H=0, uint peakCount=0):
            f0(f0),B(B),H(H),peakCount(peakCount){} };
    list<Candidate, 8> candidates;
    //static const int Pa=12,Ha=16, Pb=16,Hb=22;
    static constexpr float peakCountRatioTradeoff = 0; //(float) (Pa * Hb - Pb * Ha) / (Pb - Pa); // Ma=Mb <=> C = (Pa * Hb - Pb * Ha) / (Pb - Pa)
    static_assert(peakCountRatioTradeoff>=0,"");

    array<uint> bestPeaks0, bestPeaks;
    uint lastHarmonicRank;
    float bestF0;
#endif

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


#if 1 // Least square fuzzy energy optimization
        lastHarmonicRank = 0; lastPeak=0;
        peaks0.reserve(256); peaks.reserve(256);
        peaks0.clear(); peaks.clear(); leastSquareF0.clear();
        candidates.clear();
        float bestEnergy = 0/*, bestHPS=-inf*/; uint bestPeakCount=0;
        // Always tests both nearest rank (to F1) to avoid getting stuck in local minimum (rank shift)
#if 1
        uint nLow = max(1,int(F1/F0)), nHigh = max(nLow+2, uint(F1/F0*5/4)); //6/5));
        float f0Low = (float) F1 / nHigh;
        log(nHigh);
        for(uint n1: range(nLow,  nHigh /*+1*/)) {
            float f0 = (float) F1 / n1;
            float totalEnergy = 0; uint peakCount=0; float HPS=0;
            array<uint> peaks0, peaks, peaksLS;
            for(uint maxRank unused: maxRanks) { //for(uint maxRank: maxRanks) { //TODO: until convergence
                peaks0.clear(); peaks.clear(); peaksLS.clear(); totalEnergy = 0; peakCount=0; HPS=0; uint productCount=0;
#else
        for(uint maxRank unused: maxRanks) { //for(uint maxRank: maxRanks) { //TODO: until convergence
            uint nLow = max(1,int(F1/F0)), nHigh = nLow+1;
            //float f0Low = (float) F1 / (+1);
            for(uint n1: range(nLow, nHigh+1)) {
                float f0 = (float) F1 / n1;
#endif
                // Least square optimization of linear fit: n.f0 = f[n] <=> Xb = y (X=n, b=f0, y=f[n])
                float n2=0, nf=0; // Least square X'X and X'y coefficients
                for(uint n=1; n<=/*maxRank*/ /*maxRank*//*2*n1*/ (28/nLow)*n1; n++) {
                    // Finds local maximum around harmonic frequencies predicted by last f0 estimation
                    uint expectedF = n*f0;
                    //if(expectedF>fMax) break;
                    //if(expectedF>N/4) break;
                    uint localF = expectedF; float peakEnergy = spectrum[localF]; // Weighted by 1/(1+distance) //FIXME: integrate energy ?
                    //for(uint df=1; df < /*(uint)(f0Low/2)*//*f0Low*/f0/4; df++) { // Avoids solution only fitting the strongest peaks extremely well
                    uint df=1;
                    for(; df < f0Low/6 /*4*/; df++) { // Avoids solution only fitting the strongest peaks extremely well
                        assert_(int(expectedF-df)>=0 && expectedF+df<N/2);
                        float w = 1; //1./(1+df); //1./(1+df) - 1./(1+maxDistance);
                        // Only search below to avoid interval shift ?
                        if(w*spectrum[expectedF-df] > peakEnergy) peakEnergy=w*spectrum[expectedF-df], localF=expectedF-df;
                        if(w*spectrum[expectedF+df] > peakEnergy) peakEnergy=w*spectrum[expectedF+df], localF=expectedF+df;
                    }
                    peaks0 << expectedF;
                    //if(peakEnergy > 2*periodPower) {
                    //if(peakEnergy > periodPower/8) {
                        totalEnergy += peakEnergy;
                        if(spectrum[expectedF]>periodPower) lastHarmonicRank=max(lastHarmonicRank, n);
                        peakCount += peakEnergy > 3*periodPower;
                        assert_(peakEnergy>0);
                        if(peakEnergy > periodPower*2) { HPS += log2(peakEnergy); productCount++; }
                        peaks << localF;
                        if(peakEnergy > periodPower*2)
                            lastPeak = max(lastPeak, localF);
                        // Refines f0 with a weighted least square fit
                        /*for(; df < f0Low/3; df++) { // Search further to find correct direction for least square fit
                            assert_(int(expectedF-df)>=0 && expectedF+df<N/2);
                            float w = 1; //1./(1+df); //1./(1+df) - 1./(1+maxDistance);
                            // Only search below to avoid interval shift ?
                            if(w*spectrum[expectedF-df] > peakEnergy) peakEnergy=w*spectrum[expectedF-df], localF=expectedF-df;
                            if(w*spectrum[expectedF+df] > peakEnergy) peakEnergy=w*spectrum[expectedF+df], localF=expectedF+df;
                        }*/
                        peaksLS << localF;
                        float w = peakEnergy / n; //log2(peakEnergy); //./(1+n); //weightedEnergyDensity; // Weights by energy density, distance from integer harmonic //, rank
                        nf += w * n * localF;
                        n2 += w * sq(n);
                    //}
                    //else peaks << 0; // DEBUG: display correct rank
                }
                assert_(n2,nLow); float f0Fit = nf / n2; // Least square fit X'X b = X' y
                leastSquareF0 << f0Fit;
                this->peaks0 << copy(peaks0); this->peaks << copy(peaks); // DEBUG
                HPS /= productCount; // Normalizes HPS
                //candidates.insert(totalEnergy, Candidate(f0Fit, totalEnergy, peakCount, HPS, copy(peaks0), copy(peaks)));
                //if(totalEnergy > bestEnergy) { // Optimal peak energy
                //if(HPS > bestHPS) { // Optimal loudness (sum of log)
                //if(peakCount > bestPeakCount) { // Optimal peak count
                /*const float energyWeight = 16;
                candidates.insert(energyWeight*totalEnergy+peakCount, Candidate(f0Fit, totalEnergy, peakCount, HPS, copy(peaks0), copy(peaks)));
                if(energyWeight*totalEnergy+peakCount > energyWeight*bestEnergy+bestPeakCount) {
                    bestHPS = HPS;
                    bestPeakCount = peakCount;
                    bestEnergy = totalEnergy;
                    F0 = f0Fit;
                    this->bestF0 = F0;
                    bestPeaks0 = move(peaks0), bestPeaks = move(peaks);
                } //else break;*/
                f0 = f0Fit;
            }
            const float energyWeight = 16;
            candidates.insert(energyWeight*totalEnergy+peakCount, Candidate(f0, totalEnergy, peakCount, HPS, copy(peaks0), copy(peaks), copy(peaksLS)));
            if(energyWeight*totalEnergy+peakCount > energyWeight*bestEnergy+bestPeakCount) {
                //bestHPS = HPS;
                bestPeakCount = peakCount;
                bestEnergy = totalEnergy;
                F0 = f0;
                this->bestF0 = F0;
                bestPeaks0 = move(peaks0), bestPeaks = move(peaks);
            } //else break;
        }
        harmonicEnergy = bestEnergy;
        return F0;
#else // Exact peak count/ratio optimization
        float bestEnergy = 0, bestF0 = 0, bestB = 0; uint bestH=-1, bestPeakCount=0;
        for(uint t unused: range(2)) {
            candidates.clear();
            for(uint n1: range(F1/F0, F1/F0+1 +1)) { // Evaluates both lower/upper rank estimation
                float f0 = (float) F1 / n1; // Projects peak frequency (orthogonal (approximates L1 projection)) on f=nf0 line
                //const float B = 0;
                // Least square optimization of linear fit: n.f0 = f[n] <=> Xb = y (X=n, b=f0, y=f[n])
                float n2=0, nf=0; // Least square X'X and X'y coefficients
                float energy=0; // Evaluates energy to be used as confidence estimation by comparing against period energy (instant or mean)
                uint rankPeakCount=0, rankH=-1;
                array<uint> peaks0, peaks;
                for(uint n=1, lastH=0, peakCount=0;;n++) {
                    const uint f = round(f0*n); //*(1+B*sq(n))
                    if(f>=N/2) break;
                    energy += spectrum[f];
                    if(spectrum[f] > 2*periodPower) { // FIXME: use nearest peaks?
                        lastH = n;
                        peakCount++;
                        if((rankH+peakCountRatioTradeoff)*peakCount >= (lastH+peakCountRatioTradeoff)*rankPeakCount) { // Prevents large H increase
                            rankH=lastH, rankPeakCount=peakCount;
                        }
                        // Finds peak maximum to refine f0 with a least square fit
                        uint maxF = f; float peakEnergy = spectrum[f]; // Weighted by 1/(1+distance) //FIXME: integrate energy ?
                        for(uint df=1; df < f0/2; df++) {
                            assert_(int(f-df)>=0 && f+df<N/2, f);
                            float w = 1; //./(1+df); //1./(1+df) - 1./(1+maxDistance);
                            if(w*spectrum[f-df] > peakEnergy) peakEnergy=w*spectrum[f-df], maxF=f-df;
                            if(w*spectrum[f+df] > peakEnergy) peakEnergy=w*spectrum[f+df], maxF=f+df;
                        }
                        peaks0 << f; peaks << maxF;
                        nf += n * maxF;
                        n2 += sq(n);
                    }
                    peaks0 << 0; peaks << 0;
                }
                if((bestH+peakCountRatioTradeoff)*rankPeakCount > (rankH+peakCountRatioTradeoff)*bestPeakCount // Better merit
                        || ((bestH+peakCountRatioTradeoff)*rankPeakCount >= (rankH+peakCountRatioTradeoff)*bestPeakCount && // or same merit
                            rankPeakCount>=bestPeakCount)) { // but using more peaks
                    assert_(n2); float f0Fit = nf / n2; // Least square fit X'X b = X' y
                    bestPeaks0 = move(peaks0), bestPeaks = move(peaks);
                    bestEnergy=energy, bestF0=f0Fit, bestPeakCount=rankPeakCount, bestH=rankH; // bestB = B
                }
                candidates.insert((float)rankPeakCount/(rankH+peakCountRatioTradeoff), Candidate{f0, 0/*B*/, rankH, rankPeakCount});
            }
            F0 = bestF0;
        }
        this->bestF0 = bestF0;
        //inharmonicity = bestB;
        lastHarmonicRank = bestH;
        //peakCount = bestPeakCount;
        harmonicEnergy = bestEnergy;
        return bestF0*(1+bestB);
#endif
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+15)%12]+str(key/12-2); }

