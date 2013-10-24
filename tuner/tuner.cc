#include "thread.h"
#include "sampler.h"
#include "math.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "png.h"
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
inline float loudnessWeight(float f) {
    const float a = sq(20.6), b=sq(107.7), c=sq(737.9), d=sq(12200);
    float f2 = f*f;
    return d*f2*f2 / ((f2 + a) * sqrt((f2+b)*(f2+c)) * (f2+d));
}

/// Estimates fundamental frequency (~pitch) of test samples
struct PitchEstimation {
    VList<Plot> plots;
    Window window {&plots, int2(1024, 2*768), "Pitch, Loudness"};
    PitchEstimation() {
        Sampler sampler;
        const uint sampleRate = 48000;
        sampler.open(sampleRate, "Salamander.sfz"_, Folder("Samples"_,root()));

        const uint N = 16384; // Analysis window size (~0.7s, ~16 periods of A-1)
        FFT fft (N);

        const bool singleVelocity = false; // Tests 30 samples or 30x16 samples
        const bool skipHighest = true; // FIXME: Highest sample is quite atonal
        uint tests = 0;
        array<uint> velocityLayers;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(skipHighest && sample.pitch_keycenter >= 108) continue;
            if(singleVelocity) { if(sample.lovel > 64 || 64 > sample.hivel) continue; }
            //else { if(sample.hivel <= 33) continue; }
            int velocity = (sample.lovel+sample.hivel)/2;
            if(!velocityLayers.contains(velocity)) velocityLayers.insertSorted(velocity);
            tests++;
        }

        uint result[tests]; // Rank of actual pitch within estimated candidates
        clear(result, tests, uint(~0));
        uint testIndex=0;
        array<map<float,float>> energy; // For each note (in MIDI key), energy relative to average
        energy.grow(velocityLayers.size);
        array<map<float,float>> loudness; // For each note (in MIDI key), loudness relative to average
        loudness.grow(velocityLayers.size);
        array<map<float,float>> offsets; // For each note (in MIDI key), pitch offset (in cents) to equal temperament (A440)
        offsets.grow(velocityLayers.size);
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(skipHighest && sample.pitch_keycenter >= 108) continue;
            if(singleVelocity) { if(sample.lovel > 64 || 64 > sample.hivel) continue; }
            //else { if(sample.hivel <= 33) continue; }
            int expectedKey = sample.pitch_keycenter;
            int velocity = (sample.lovel+sample.hivel)/2;

            assert(N<=sample.flac.duration);
            buffer<float2> stereo = decodeAudio(sample.data, N);
            float signal[N];
            for(uint i: range(N)) signal[i] = (stereo[i][0]+stereo[i][1])/(2<<(24+1));
            ref<float> halfcomplex = fft.transform(signal);
            buffer<float> spectrum (N/2);
            for(int i: range(N/2)) {
                spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum

            }
            float e0=0; for(uint i: range(N/2)) e0 += spectrum[i];
            float l0=0; for(uint i: range(N/2)) l0 += loudnessWeight(i)*spectrum[i];
            // Estimates candidates using maximum peak
            const uint fMin = N*440*pow(2, -4 - 0./12 - (1./2 / 12))/sampleRate; // ~27 Hz ~ half pitch under A-1
            const uint fMax = N*440*pow(2,  3 + 3./12 + (1./2 / 12))/sampleRate; // ~4308 Hz ~ half pitch over C7
            int fPeak=0; float maxPeak=0; for(uint i=fMin; i<=fMax; i++) if(spectrum[i]>maxPeak) maxPeak=spectrum[i], fPeak=i;
            // Use autocorrelation to find best match between f, f/2, f/3, f/4
            const float kPeak = (float)N/fPeak; // k represents periods here (and not 1/wavelength­)
            const int kMax = round(4*kPeak);
            float kNCC = kPeak;
            float maxNCC=0;
            int iNCC=1;
            if(kPeak > 32) { // High pitches are accurately found by spectrum peak picker (autocorrelation will match lower octaves)
                for(uint i=1; i<=4; i++) { // Search lower octaves for best correlation
                    float bestK = i*kPeak;
                    int k0 = round(bestK);
                    float sum=0; for(uint i: range(N-kMax)) sum += signal[i]*signal[k0+i];
                    float max = sum;
                    if(max > maxNCC) maxNCC = max, kNCC = bestK, iNCC=i;
                }
                for(int k=round(iNCC*kPeak)-1;;k--) { // Scans backward (decreasing k) until local maximum to estimate subkey pitch
                    float sum=0; for(uint i: range(N-kMax)) sum += signal[i]*signal[k+i];
                    if(sum > maxNCC) maxNCC = sum, kNCC = k;
                    else break;
                }
                for(int k=round(iNCC*kPeak)+1;;k++) { // Scans forward (increasing k) until local maximum to estimate subkey pitch
                    float sum=0; for(uint i: range(N-kMax)) sum += signal[i]*signal[k+i];
                    if(sum > maxNCC) { /*log("+",sum, maxNCC, iNCC*kPeak, kNCC, k);*/ maxNCC = sum, kNCC = k; } // 7 / 464
                    else break;
                }
            }
            float expectedK = sampleRate/keyToPitch(expectedKey);
            int key = round(pitchToKey(sampleRate/kNCC));
            if(key==expectedKey) {
                result[testIndex] = 0;
                offsets[velocityLayers.indexOf(velocity)].insert(key, 100*12*log2(expectedK/kNCC));
                energy[velocityLayers.indexOf(velocity)].insert(key, e0);
                loudness[velocityLayers.indexOf(velocity)].insert(key, l0);
                //if(iNCC) log(iNCC, kNCC-int(iNCC*kPeak), kNCC/(iNCC*kPeak));
            } else {
                log(">", expectedKey, expectedK, sample.lovel, sample.hivel);
                log("?", iNCC, maxPeak, kPeak, iNCC*kPeak, maxNCC, kNCC, key);
            }
            testIndex++;
        }
        {float sum=0, count=0;
        for(const auto& e: energy) for(float e0: e.values) sum+=e0, count++;
        for(auto& e: energy) for(float& e0: e.values) e0 = 10*log10(e0/(sum/count));}
        {float sum=0, count=0;
        for(const auto& e: loudness) for(float e0: e.values) sum+=e0, count++;
        for(auto& e: loudness) for(float& e0: e.values) e0 = 10*log10(e0/(sum/count));}
        uint success[4] = {};
        buffer<char> detail(tests);
        for(int i : range(tests)) {
            int rank = result[i];
            for(uint j: range(4)) success[j] += uint(rank)<j+1;
            assert_(rank>=-1 && rank<8, rank);
            detail[i] = "X12345678"[rank+1];
        }
        String s;
        for(uint j: range(4)) if(j==0 || success[j-1]<success[j]) s<<str(j+1)+": "_<<str(success[j])<<", "_; s.pop(); s.pop();
        log(detail, "("_+s+")/"_,tests);
        // 16K: 453; 32K: 455 / 464
        {Plot plot;
            plot.title = String("Pitch ratio (in cents) to equal temperament (A440)"_);
            plot.xlabel = String("Key"_), plot.ylabel = String("Cents"_);
            plot.legends = apply(velocityLayers, [](uint velocity){return str(velocity);});
            plot.dataSets = move(offsets);
            plots << move(plot);
        }
        {Plot plot;
            plot.title = String("Energy ratio (in decibels) to average energy over all samples"_);
            plot.xlabel = String("Key"_), plot.ylabel = String("Decibels"_);
            plot.legends = apply(velocityLayers, [](uint velocity){return str(velocity);});
            plot.dataSets = move(energy);
            plot.legendPosition = Plot::BottomRight;
            plots << move(plot);
        }
        /*{Plot plot;
            plot.title = String("Loudness ratio (in decibels) to average loudness over all samples"_);
            plot.xlabel = String("Key"_), plot.ylabel = String("Decibels"_);
            plot.legends = apply(velocityLayers, [](uint velocity){return str(velocity);});
            plot.dataSets = move(loudness);
            plot.legendPosition = Plot::BottomRight;
            plots << move(plot);
        }*/
        window.backgroundColor=window.backgroundCenter=1;
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(PrintScreen).connect([=]{
            writeFile("pitch.png"_, encodePNG(renderToImage(plots[0], int2(1024,768))), home());
            writeFile("energy.png"_, encodePNG(renderToImage(plots[1], int2(1024,768))), home());
            writeFile("loudness.png"_, encodePNG(renderToImage(plots[2], int2(1024,768))), home());
        });
        //window.localShortcut(PrintScreen)();
    }
} test;
