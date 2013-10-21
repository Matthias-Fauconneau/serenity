#include "thread.h"
#include "sampler.h"
#include "math.h"
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
    FFT(uint N) : N(N) { for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2; }
    ref<float> transform(const ref<float>& signal) {
        assert(N <= signal.size);
        for(uint i: range(N)) windowed[i] = hann[i]*signal[i]; // Multiplies window
        fftwf_execute(fftw); // Transforms (FIXME: use execute_r2r and free windowed/transform buffers between runs)
        return halfcomplex;
    }
};

template<Type V, uint N> struct list { // Small sorted list
    static constexpr uint size = N;
    struct element { float key; V value; element(float key=0, V value=0):key(key),value(value){} } elements[N];
    void insert(float key, V value) {
        int i=0; while(i<N && elements[i].key<key) i++; i--;
        if(i<0) return; // New candidate would be lower than current
        for(uint j: range(i)) elements[j]=elements[j+1]; // Shifts left
        elements[i] = element(key, value); // Inserts new candidate
    }
    const element& operator[](uint i) { assert(i<N); return elements[i]; }
    const element& last() { return elements[N-1]; }
    const element* begin() { return elements; }
    const element* end() { return elements+N; }
};
template<Type V, uint N> String str(const list<V,N>& a) {
    String s; for(uint i: range(a.size)) { s<<str(a.elements[i].key, a.elements[i].value); if(i<a.size-1) s<<", "_;} return s;
}

/// Numeric range
struct reverse_range {
    reverse_range(range r) : start(r.stop), stop(r.start){}
    struct iterator {
        int i;
        int operator*() { return i; }
        iterator& operator++() { i--; return *this; }
        bool operator !=(const iterator& o) const{ return i>=o.i; }
    };
    iterator begin() const { return {start}; }
    iterator end() const { return {stop}; }
    int start, stop;
};

/// Reverse iterator
generic struct reverse {
    reverse(const ref<T>& a) : start(a.end()-1), stop(a.begin()) {}
    struct iterator {
        const T* pointer;
        const T& operator*() { return *pointer; }
        iterator& operator++() { pointer--; return *this; }
        bool operator !=(const iterator& o) const { return pointer>=o.pointer; }
    };
    iterator begin() const { return {start}; }
    iterator end() const { return {stop}; }
    const T* start;
    const T* stop;
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }

/// Estimates fundamental frequency (~pitch) of test samples
struct PitchEstimation {
    PitchEstimation() {
        Sampler sampler;
        const uint sampleRate = 48000;
        sampler.open(sampleRate, "Salamander.sfz"_, Folder("Samples"_,root()));

        const uint N = 32768; // Analysis window size (16 periods of A-1)
        FFT fft (N);

        const bool singleVelocity = true; // Tests 30 samples or 30x16 samples (TODO: fix failing low/high velocities)
        uint tests = 0;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(singleVelocity && (sample.lovel > 64 || 64 > sample.hivel)) continue;
            tests++;
        }

        uint result[tests]; // Rank of actual pitch within estimated candidates
        clear(result, tests, uint(~0));
        uint testIndex=0;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(singleVelocity && (sample.lovel > 64 || 64 > sample.hivel)) continue;
            int expectedKey = sample.pitch_keycenter;
            float expectedF0 = keyToPitch(expectedKey);
            //log("key "_+str(key)+", f0="_+pad(ftoa(expectedF0,1),6)+" Hz, vel=["_+str(sample.lovel)+", "_+str(sample.hivel)+"]"_);

            assert(N<=sample.flac.duration);
            buffer<float2> stereo = decodeAudio(sample.data, N);
            float signal[N];
            for(uint i: range(N)) signal[i] = (stereo[i][0]+stereo[i][1])/(2<<(24+1));
#if 1 // 1: 21/30 (smooth: 2->1), 2: 26/30 (fmax/2 4X->2), 3: 28/30 ((fmax+1)/3: 2X->3), 4: 29/30 ((fmax+1)/3: 2X->3)
            ref<float> halfcomplex = fft.transform(signal);
            buffer<float> spectrum (N/2);
            for(int i: range(N/2)) spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum

            // Estimates fundamental frequency using highest peak (best correlation with sin, FIXME: harmonic/autocorrelation)
            const uint fMin = 18; // ~27 Hz ~ semi pitch under A-1
            const uint fMax = 2941; // ~4186 Hz ~ semi pitch over C7
            list<uint, 8> candidates;
            float maximum=0;
            for(uint i=fMin; i<=fMax; i++) {
                if(spectrum[i-1] <= spectrum[i] && spectrum[i] >= spectrum[i+1]) {
                    float merit = 0;
                    {const int smooth=1; for(int di=-smooth; di<=smooth; di++) merit += spectrum[i+di];}
                    maximum = max(maximum, merit);
                    candidates.insert(merit, i);
                }
            }
            candidates.insert(maximum, candidates.last().value/2);
            candidates.insert(maximum, round((candidates.last().value+1)/3.));
            candidates.insert(maximum, round((candidates.last().value+1)/4.));

            for(uint rank: range(candidates.size)) {
                uint i = candidates[candidates.size-1-rank].value;
                if(!i) continue; // Less local maximum than candidates
                int key = min((int)round(pitchToKey((float)i*sampleRate/N)), 108);
                if(key==expectedKey && rank<result[testIndex]) result[testIndex] = rank;
            }
            if(result[testIndex] != 0) {
                log(">", expectedKey, expectedF0*N/sampleRate);
                for(uint rank: range(candidates.size)) {
                    uint i = candidates[candidates.size-1-rank].value; float v=candidates[candidates.size-1-rank].key/maximum;
                    if(!i) continue; // Less local maximum than candidates
                    int key = min((int)round(pitchToKey((float)i*sampleRate/N)), 108);
                    log(key==expectedKey?'!':'?', v, i, key);
                }
            }
#else // 1: 25/30 (top 5), 2: 27/30
            // Exhaustive normalized cross correlation search
            const uint kMin = 11; // ~4364 Hz ~ floor(rate/C7)
            const uint kMax = 1792; // ~27 Hz ~ A-1 · 2^(-1/2/12)
            float e0=0; for(uint i: range(N-kMax)) e0 += sq(signal[i]); // Total energy
            //float maximum=0; uint bestK=0;
            list<uint, 8> candidates;
            float NCC[2]={0,0};
            for(uint k=kMax; k>=kMin; k--) { // Search periods from long to short
                float ek=0; float ec=0;
                for(uint i: range(N-kMax)) {
                    ec += signal[i]*signal[k+i]; // Correlation
                    ek += sq(signal[k+i]); // Total energy
                }
                ec /= sqrt(e0*ek); // Normalizes
                if((ec <= NCC[0] && NCC[0] >= NCC[1]) || (k<=kMin+3)) // Local maximum
                    candidates.insert(NCC[0], k+1);
                if(k==2*kMin && candidates.last().key > 1./2 && candidates.last().value > kMax/2) goto done; // Skips short periods which would replace a good long period match
                NCC[1] = NCC[0];
                NCC[0] = ec;
            }
            if(NCC[0]>=NCC[1]) candidates.insert(NCC[0], kMin);
            done:;
            for(uint rank: range(candidates.size)) {
                uint k = candidates[candidates.size-1-rank].value;
                if(!k) continue; // Less local maximum than candidates
                int key = min((int)round(pitchToKey((float)sampleRate/k)), 108); // 11 samples rounds to #C7
                if(key==expectedKey && rank<result[testIndex]) result[testIndex] = rank;
            }
            if(result[testIndex] != 0) {
                log(">", expectedKey, sampleRate/expectedF0);
                for(uint rank: range(candidates.size)) {
                    uint k = candidates[candidates.size-1-rank].value; float v=candidates[candidates.size-1-rank].key;
                    if(!k) continue; // Less local maximum than candidates
                    int key = min((int)round(pitchToKey((float)sampleRate/k)), 108); // 11 samples rounds to #C7
                    log(key==expectedKey?'!':'?', v, k, key);
                }
            }
#endif
            testIndex++;
        }
        int success[8] = {};
        buffer<char> detail(tests);
        for(int i : range(tests)) {
            int rank = result[i];
            for(uint j: range(8)) success[j] += uint(rank)<j+1;
            assert_(rank>=-1 && rank<16, rank);
            detail[i] = "X123456789ABCDEF"[rank+1];
        }
        String s;
        for(uint j: range(8)) if(j==0 || success[j-1]<success[j]) s<<str(j+1)+": "_<<str(success[j])<<", "_; s.pop(); s.pop();
        log(detail, "("_+s+")"_);
    }
} test;
