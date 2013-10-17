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
    buffer<float> transform(const ref<float>& signal) {
        assert(N <= signal.size);
        for(uint i: range(N)) windowed[i] = hann[i]*signal[i]; // Multiplies window
        fftwf_execute(fftw); // Transforms (FIXME: use execute_r2r and free windowed/transform buffers between runs)
        buffer<float> spectrum {N/2};
        for(int i: range(N/2)) spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum
        return spectrum;
    }
};

template<Type V, uint N> struct list { // Small sorted list
    static constexpr uint size = N;
    struct element { float key; V value; element(float key=0, V value=0):key(key),value(value){} } elements[N];
    void insert(float key, V value) {
        int i=0; while(i<N && elements[i].key<=key) i++; i--;
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

        const bool singleVelocity = true; // Tests 30 samples or 30x16 samples (TODO: fix failing low/high velocities)
        int tests = 0;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(singleVelocity && (sample.lovel > 64 || 64 > sample.hivel)) continue;
            tests++;
        }

        const int periodMax = 1; //2; // 16,32 K
        const int smoothMax = 0; //3; // 0,2,4 Hz
        const int octaveMax = 1; // 1,2,3 octaves
        int result[periodMax][octaveMax][smoothMax+1][tests]; // Rank of actual pitch within estimated candidates
        clear((int*)result, periodMax*octaveMax*(smoothMax+1)*tests, -1);
        uint testIndex=0;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(singleVelocity && (sample.lovel > 64 || 64 > sample.hivel)) continue;
            int expectedKey = sample.pitch_keycenter;
            float expectedF0 = keyToPitch(expectedKey);
            //log("key "_+str(key)+", f0="_+pad(ftoa(expectedF0,1),6)+" Hz, vel=["_+str(sample.lovel)+", "_+str(sample.hivel)+"]"_);

            assert(32768<=sample.flac.duration);
            buffer<float2> stereo = decodeAudio(sample.data, 32768);
            float signal[32768];
            for(uint i: range(32768)) signal[i] = (stereo[i][0]+stereo[i][1])/(2<<(24+1));
#if 0 // 20/30
            for(uint period: range(periodMax)) {
                static FFT ffts[2] = {/*8192,*/16384,32768};
                FFT& fft = ffts[period];
                uint N = fft.N;
                buffer<float> spectrum = fft.transform(signal);

                // Estimates fundamental frequency using spectral harmonic correlation
                for(uint octaveCount=1; octaveCount<=octaveMax; octaveCount++) {
                    for(int smoothRadius=0; smoothRadius<=smoothMax; smoothRadius++) {
                        list<int, 6> candidates;
                        for(int i: range(smoothRadius,N/2/octaveCount-smoothRadius)) {
                            float s=0;
                            for(int di=-smoothRadius; di<=smoothRadius; di++) {
                                float p=1;
                                for(uint r=1; r<=octaveCount; r++) p *= spectrum[r*i+di];
                                s += p;
                            }
                            candidates.insert(s, i); //FIXME: only local maximums
                        }
                        candidates.insert(candidates.last().key/4, candidates.last().value/2); // Inserts half frequency candidate
                        log(candidates);
                        //TODO: Double frequence, median of last 7 ? FIXME: average/deviation in frequence or period ?
                        //float mean=0; for(Element e: candidates) mean += e.value; mean /= candidates.size;
                        //float deviation=0; for(Element e: candidates) deviation += sq(e.value-mean); deviation /= candidates.size;
                        // Normalized cross correlation function
                        array<range> ranges; // Search ranges
                        {int min=N,max=0;
                            for(auto e: candidates) { assert(e.value); min=::min(min, e.value), max=::max(max, e.value); }
                            assert(min);
                            ranges << range(min, max); //FIXME: search around 2sigma of each candidates (within [A-1, C7])
                        }
                        int last=ranges.first().stop; int total=0;
                        for(range r: ranges.slice(1)) { assert_(last < r.start); total+=r.stop-r.start; last=r.stop; }
                        int min=ranges.first().start, max=ranges.last().stop;
                        log(octaveCount, "min",min,"max", max,"size", max-min);
                        float e0=0; for(uint i: range(N-max)) e0 += sq(signal[i]); // Total energy
                        float maximum=0; uint bestK=0;
                        for(range r: reverse<range>(ranges)) for(uint k: reverse_range(r)) { // Search periods from long to short
                            float ek=0; float ec=0;
                            for(uint i: range(N-max)) {
                                ec += signal[i]*signal[k+i]; // Correlation
                                ek += sq(signal[k+i]); // Total energy
                            }
                            ec /= sqrt(e0*ek); // Normalizes
                            if(ec > maximum) maximum=ec, bestK=k;
                            if(ec > 0.85) goto done; // Prevents halving
                        }
                        done:;
                        log(bestK);
                        if(bestK) result[period][octaveCount-1][smoothRadius][testIndex] = round(pitchToKey(sampleRate/bestK))==key;
                    }
                }
            }
#else // 21/30 ~ 70%
            // Exhaustive normalized cross correlation search
            const uint kMin = 11; // ~4364 Hz ~ floor(rate/C7)
            const uint kMax = 1792; // ~27 Hz ~ A-1 · 2­^(-1/2/12)
            const uint N = 32768; // kMax*2; ?
            float e0=0; for(uint i: range(N-kMax)) e0 += sq(signal[i]); // Total energy
            //float maximum=0; uint bestK=0;
            list<int, 7> candidates;
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
                //if(ec > maximum) maximum=ec, bestK=k;
                //if(ec > 0.88) break; // Prevents low (long) notes to match better with very short (high) k (breaks all other)
                NCC[1] = NCC[0];
                NCC[0] = ec;
            }
            if(NCC[0]>=NCC[1]) candidates.insert(NCC[0], kMin);
            //log(candidates);
            //uint bestK = candidates.last().value;
            log("?", expectedKey, sampleRate/expectedF0);
            for(uint rank: range(candidates.size)) {
                uint k = candidates[candidates.size-1-rank].value; float v=candidates[candidates.size-1-rank].key;
                if(!k) continue; // Less local maximum than candidates
                int key = min((int)round(pitchToKey((float)sampleRate/k)), 108); // 11 samples rounds to #C7
                log("!", v, k, key);
                if(key==expectedKey && result[0][0][0][testIndex]==-1) result[0][0][0][testIndex] = rank;
            }
#endif
            testIndex++;
        }
        int bestScore=0;
        for(uint period: range(periodMax)) {
            for(uint octaveCount=1; octaveCount<=octaveMax; octaveCount++) { // 1: Estimates directly from intensity
                for(int smoothRadius=0; smoothRadius<=smoothMax; smoothRadius++) {
                    int success=0; for(int i : range(tests)) success += (result[period][octaveCount-1][smoothRadius][i]==0);
                    bestScore = max(bestScore, success);
                }
            }
        }
        for(uint period: range(periodMax)) {
            for(uint octaveCount=1; octaveCount<=octaveMax; octaveCount++) { // 1: Estimates directly from intensity
                for(int smoothRadius=0; smoothRadius<=smoothMax; smoothRadius++) {
                    int success=0; buffer<char> detail(tests);
                    for(int i : range(tests)) {
                        int rank = result[period][octaveCount-1][smoothRadius][i];
                        success += rank==0;
                        assert_(rank>=-1 && rank<16, rank);
                        detail[i] = "X0123456789ABCDEF"[rank+1];
                    }
                    if(success >= bestScore*14/15)
                        log(/*"N",16384*pow(2,period), "\tO",octaveCount, "\tR",smoothRadius, "\t->", success, "\t",*/ detail);
                }
            }
        }
        log(bestScore,"/",tests,"~",str((int)round(100.*bestScore/tests))+"%"_);
    }
} test;
