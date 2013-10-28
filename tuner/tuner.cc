#include "thread.h"
#include "sampler.h"
#include "math.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "png.h"

bool operator ==(range a, range b) { return a.start==b.start && a.stop==b.stop; }

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

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline float loudnessWeight(float f) {
    const float a = sq(20.6), b=sq(107.7), c=sq(737.9), d=sq(12200);
    float f2 = f*f;
    return d*f2*f2 / ((f2 + a) * sqrt((f2+b)*(f2+c)) * (f2+d));
}
String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+3)%12]+str((key-33)/12); }

/// Estimates fundamental frequency (~pitch) of test samples
struct PitchEstimation {
    VList<Plot> plots;
    Window window {&plots, int2(1024, 768), "Tuner"};
    PitchEstimation() {
        Sampler sampler;
        const uint rate = 48000;
        sampler.open(rate, "Salamander.original.sfz"_, Folder("Samples"_,root()));

        array<range> velocityLayers;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(!velocityLayers.contains(range(sample.lovel,sample.hivel+1))) velocityLayers << range(sample.lovel,sample.hivel+1);
        }
        const uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
        FFT fft (N);

        array<map<float,float>> energy; energy.grow(velocityLayers.size); // For each note (in MIDI key), energy relative to average
        array<map<float,float>> offsets; offsets.grow(velocityLayers.size); // For each note (in MIDI key), pitch offset (in cents) to equal temperament
        uint success = 0, total = 0;
        String results;
        float octaveRange[4][4]={{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0}}; // {min true (require), min false, max false, max true (doesn't need))
        for(int velocityLayer: range(velocityLayers.size)) {
            String layerResults;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger!=0) continue;
                if(velocityLayers[velocityLayer] != range(sample.lovel,sample.hivel+1)) continue;

                int expectedKey = sample.pitch_keycenter;
                float expectedF = N*keyToPitch(expectedKey)/rate;
                float expectedK = rate/keyToPitch(expectedKey);

                assert(N<=sample.flac.duration);
                buffer<float2> stereo = decodeAudio(sample.data, N);
                float signal[N];
                for(uint i: range(N)) signal[i] = (stereo[i][0]+stereo[i][1])/(2<<(24+1));
                ref<float> halfcomplex = fft.transform(signal);
                buffer<float> spectrum (N/2);
                float e = 0;
                for(int i: range(N/2)) {
                    spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum
                    e += spectrum[i];
                }
                const uint fMin = N*440*pow(2, -4 - 0./12 - (1./2 / 12))/rate; // ~27 Hz ~ half pitch under A-1
                const uint fMax = N*440*pow(2,  3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half pitch over C7
                // Estimates candidates using maximum peak
                //float maxPeak=0; uint fPeak=0; for(uint i=fMin; i<=fMax; i++) if(spectrum[i]>maxPeak) maxPeak=spectrum[i], fPeak=i;
                list<uint,8> peaks;
                float eb=0; for(uint i=fMin; i<=fMax; i++) eb += spectrum[i];
                for(uint i=fMin; i<=fMax; i++) if(spectrum[i-1] <= spectrum[i] && spectrum[i] >= spectrum[i+1]) peaks.insert(spectrum[i]/eb, i);
                float maxPeak = peaks.last().key; uint fPeak = peaks.last().value;
                // Use autocorrelation to find best match between f, f/2, f/3, f/4
                const float kPeak = (float)N/fPeak; // k represents periods here (and not 1/wavelength­)
                const int kMax = round(4*kPeak);
                float kNCC = kPeak;
                float maxNCC=0;
                int iNCC=1;
                list<uint,4> octaves;
                if(kPeak > 32) { // High pitches are accurately found by spectrum peak picker (autocorrelation will match lower octaves)
                    for(uint i=1; i<=4; i++) { // Search lower octaves for best correlation
                        float bestK = i*kPeak;
                        //if(round(pitchToKey(rate/bestK))<20) break; // Would match pitches lower than possible keys
                        int k = round(bestK);
                        float sum=0; for(uint i: range(N-kMax)) sum += signal[i]*signal[k+i];
                        sum /= 1+i/512.; // Penalizes longer periods to avoid very slightly higher correlation to trigger spurious octave shifts
                        octaves.insert(sum, i);
                        if(sum > maxNCC) maxNCC = sum, kNCC = bestK, iNCC=i;
                    }
                }

                int key = round(pitchToKey(rate/kNCC));
                char result = key==expectedKey ? str(iNCC)[0] : '0';
                String refine;
                if(kPeak > 32) {
                    for(int k=round(iNCC*kPeak)-1;k>=8;k--) { // Scans backward (decreasing k) until local maximum to estimate subkey pitch
                        float sum=0; for(uint i: range(N-kMax)) sum += signal[i]*signal[k+i];
                        if(sum > maxNCC) {
                            if(k<round(kNCC)-1) refine = str("-", "sum:", sum, "max:", maxNCC, "ikPeak:", iNCC*kPeak, "kNCC:", kNCC, "k:", k);
                            maxNCC = sum, kNCC = k;
                        }
                        else if(k<round(iNCC*kPeak)-32) // Exhaustive local search (Fix accuracy of low pitches) (+2)
                            break;
                    }
                    for(int k=round(iNCC*kPeak)+1;;k++) { // Scans forward (increasing k) until local maximum to estimate subkey pitch
                        float sum=0; for(uint i: range(N-kMax)) sum += signal[i]*signal[k+i];
                        if(sum > maxNCC) {
                            if(k>kNCC+1) refine = str("+", "sum:", sum, "max:", maxNCC, "ikPeak:", iNCC*kPeak, "kNCC:", kNCC, "k:", k);
                            maxNCC = sum, kNCC = k;
                        }
                        else break;
                    }
                    bool before = key==expectedKey;
                    key = round(pitchToKey(rate/kNCC));
                    if(before) assert_(key==expectedKey);
                    if(!before && key == expectedKey) result = 'R';
                }

                energy[velocityLayer].insert(expectedKey, e);
                if(key==expectedKey || abs(log2(expectedK/kNCC))<1./(2*12)) offsets[velocityLayer].insert(expectedKey, 100*12*log2(expectedK/kNCC));
                if(key!=expectedKey || refine /*|| iNCC>1*/)
                    log(hex(velocityLayer)+">"_, expectedKey, "["_+strKey(expectedKey)+"]"_, "~ f:", expectedF, "k:", expectedK);
                //if(key==expectedKey && iNCC>1) log("i", iNCC, kNCC-int(iNCC*kPeak), kNCC/(iNCC*kPeak));
                if(key!=expectedKey)
                    log("?", "iNCC", iNCC, "maxPeak", maxPeak, "kPeak", kPeak, "ikPeak", iNCC*kPeak, "maxNCC", maxNCC, "kNCC", kNCC, "key", key);
                if(key!=expectedKey /*|| iNCC>1*/) log(octaves, 100*octaves[3].key/octaves[2].key);
                if(key!=expectedKey /*|| iNCC>1*/) log(peaks);
                if(refine) log(refine);
                layerResults << result;
                total++; if(key==expectedKey) success++;

                if(key==expectedKey) {
                    for(int i: range(4)) {
                        if(round(pitchToKey((float)rate*fPeak/N/(i+1)))==expectedKey) octaveRange[i][0] = ::min(octaveRange[i][0], maxPeak);
                        if(round(pitchToKey((float)rate*fPeak/N/(i+1)))!=expectedKey) octaveRange[i][1] = ::min(octaveRange[i][1], maxPeak);
                        if(round(pitchToKey((float)rate*fPeak/N/(i+1)))!=expectedKey) octaveRange[i][2] = ::max(octaveRange[i][2], maxPeak);
                        if(round(pitchToKey((float)rate*fPeak/N/(i+1)))==expectedKey) octaveRange[i][3] = ::max(octaveRange[i][3], maxPeak);
                    }
                }
            }
            log(layerResults);
            results << layerResults<<'\n';
        }
        log("------------------------------------");
        log(results, success,"/",total); // 16K: 457; 32K: 455 / 464
        log(octaveRange[0]);
        log(octaveRange[1]);
        log(octaveRange[2]);
        log(octaveRange[3]);

        /*const float e0 = mean(energy.last().values); // Computes mean energy of highest velocity layer
        for(auto& layer: energy) for(float& e: layer.values) e /= e0; // Normalizes all energy values
        {// Flattens all samples to same level using SFZ "volume" attribute
            String sfz;
            // Keys with dampers
            sfz << "<group> ampeg_release=1\n"_;
            float maxGain = 0;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger!=0 || sample.hikey>88) continue;
                int velocityLayer = velocityLayers.indexOf(range(sample.lovel,sample.hivel+1));
                float e = energy[velocityLayer].at(sample.pitch_keycenter);
                maxGain = max(maxGain, sqrt(1/e));
                sfz << "<region> sample="_+sample.name+" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                       +" lovel="_+str(sample.lovel)+" hivel="_+str(sample.hivel)+" pitch_keycenter="_+str(sample.pitch_keycenter)+
                       " volume="_+str(10*log10(1/e))+"\n"_; // 10 not 20 as energy is already squared
            }
            // Keys without dampers
            sfz << "<group> ampeg_release=0\n"_;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger!=0 || sample.hikey<=88) continue;
                int velocityLayer = velocityLayers.indexOf(range(sample.lovel,sample.hivel+1));
                float e = energy[velocityLayer].at(sample.pitch_keycenter);
                maxGain = max(maxGain, sqrt(1/e));
                sfz << "<region> sample="_+sample.name+" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                       +" lovel="_+str(sample.lovel)+" hivel="_+str(sample.hivel)+" pitch_keycenter="_+str(sample.pitch_keycenter)+
                       " volume="_+str(10*log10(1/e))+"\n"_; // 10 not 20 as energy is already squared
            }
            // Release samples
            sfz << "<group> trigger=release\n"_;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger==0) continue;
                assert(sample.hikey<=88); // Keys without dampers
                sfz << "<region> sample="_+sample.name+" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                       +" lovel="_+str(sample.lovel)+" hivel="_+str(sample.hivel)+" pitch_keycenter="_+str(sample.pitch_keycenter)+"\n"_;
            }
            writeFile("Salamander."_+str(N)+".sfz"_,sfz,Folder("Samples"_));
        }*/

        /*{
            for(auto& e: energy) for(float& k: e.keys) k -= 21; // A0 -> 0
            for(auto& e: energy) for(float& y: e.values) y = 10*log10(y); // Decibels
            {Plot plot;
                plot.title = String("Energy"_);
                plot.xlabel = String("Key"_), plot.ylabel = String("Decibels"_);
                plot.legends = apply(velocityLayers, [](range velocity){return str(velocity.stop);});
                plot.dataSets = copy(energy);
                plots << move(plot);
            }
        }*/
        /*{Plot plot;
            plot.title = String("Pitch ratio (in cents) to equal temperament (A440)"_);
            plot.xlabel = String("Key"_), plot.ylabel = String("Cents"_);
            plot.legends = apply(velocityLayers, [](range velocity){return str(velocity.stop);});
            plot.dataSets = move(offsets);
            plots << move(plot);
        }
        window.backgroundColor=window.backgroundCenter=1;
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(PrintScreen).connect([=]{
            writeFile("energy.png"_, encodePNG(renderToImage(plots[0], int2(1024,768))), home());
            writeFile("output.png"_, encodePNG(renderToImage(plots[1], int2(1024,768))), home());
            writeFile("offset.png"_, encodePNG(renderToImage(plots[2], int2(1024,768))), home());
        });*/
        //window.localShortcut(PrintScreen)();
    }
} test;
