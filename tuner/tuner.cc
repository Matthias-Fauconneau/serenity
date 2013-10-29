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

        array<range> velocityLayers; array<int> keys;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(!velocityLayers.contains(range(sample.lovel,sample.hivel+1))) velocityLayers << range(sample.lovel,sample.hivel+1);
            if(!keys.contains(sample.pitch_keycenter)) keys << sample.pitch_keycenter;
        }

        const uint N = 8192; // Analysis window size (A-1 (27Hz~2K))
        FFT fft (N);
        const uint fMin = N*440*pow(2, -4 - 0./12 - (1./2 / 12))/rate; // ~27 Hz ~ half pitch under A-1
        const uint fMax = N*440*pow(2,  3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half pitch over C7

        array<map<float,float>> energy; energy.grow(velocityLayers.size); // For each note (in MIDI key), energy relative to average
        array<map<float,float>> offsets; offsets.grow(velocityLayers.size); // For each note (in MIDI key), pitch offset (in cents) to equal temperament
        uint success = 0, total = 0;
        String results;
        map<float, float> spectrumPlot;
        for(int velocityLayer: range(velocityLayers.size)) {
            String layerResults;
            for(int keyIndex: range(keys.size)) {
                const Sample& sample =
                        *({const Sample* sample = 0;
                           for(const Sample& s: sampler.samples) {
                               if(s.trigger!=0) continue;
                               if(range(s.lovel,s.hivel+1) != velocityLayers[velocityLayer]) continue;
                               if(s.pitch_keycenter != keys[keyIndex]) continue;
                               assert(!sample);
                               sample = &s;
                           }
                           assert_(sample, velocityLayer, keyIndex, velocityLayers[velocityLayer].start, velocityLayers[velocityLayer].stop, keys[keyIndex]);
                           sample;
                          });

                int expectedKey = sample.pitch_keycenter;
                //if(expectedKey==108) continue;
                float expectedF = N*keyToPitch(expectedKey)/rate;
                float expectedK = rate/keyToPitch(expectedKey);

                assert(N<=sample.flac.duration);
                buffer<float2> stereo = decodeAudio(sample.data, N);
                float signal[N];
                for(uint i: range(N)) signal[i] = (stereo[i][0]+stereo[i][1])/(2<<(24+1));
                ref<float> halfcomplex = fft.transform(signal);
                buffer<float> spectrum (N/2);
                float e = 0;
                for(uint i: range(N/2)) {
                    spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum
                    e += spectrum[i];
                }

                // Selects two maximum peaks
                float firstPeak = 0; uint fPeak = 0; uint highF = 0;
                for(uint i=fMin; i<=fMax; i++) {
                    if(spectrum[i] > firstPeak) firstPeak = spectrum[i], fPeak = i;
                    if(spectrum[i] > firstPeak/8) highF = i; // Highest pitch peak > 1./8 maximum firstPeak
                    if(expectedKey==108 && velocityLayer==15)
                        if(i>=fMin && i<=fMax)
                            if(spectrum[i-1] <= spectrum[i] && spectrum[i] >= spectrum[i+1])
                                spectrumPlot.insert(i) = spectrum[i];
                }
                if(16*highF > N) fPeak = highF; // Selects highest pitch peak if it is over 3000Hz

                // Use autocorrelation to find best match between f, f/2, f/3, f/4
                const float kPeak = (float)N/fPeak; // k represents periods here (and not 1/wavelength­)
                float kNCC = kPeak;
                float maxNCC=0;
                int iNCC=1;

                String debug;
                if(32*highF < N) { // High pitches are accurately found by spectrum peak picker (autocorrelation will match lower octaves under 1500Hz)
                    for(uint i=1; i <= ((64*highF < N) ? 5 : 2); i++) { // Search lower octaves for best correlation (only first peak) (-3+2), 8K: 5th (+1)
                        float bestK = i*kPeak;
                        int k0 = round(bestK);
                        for(int k=k0;k>0;k--) { // Scans backward (decreasing k) until local maximum
                            float sum=0; for(uint i: range(N-k0)) sum += signal[i]*signal[k+i]; // N-k0 instead of N-kMax to avoid some doubling
                            sum *= 1 - i*(N/8192)/96.; // Penalizes to avoid some period doubling (overly sensitive) (8K: +2)
                            if(sum > maxNCC) maxNCC = sum, kNCC = k, iNCC=i;
                            else if(k<k0*31/32) // 8K: +20
                                break;
                        }
                    }
                    if(64*highF > N) { // Exhaustive search starting from highK/3 to match C7
                        float bestK = N/(highF*3);
                        int k0 = round(bestK);
                        for(int k=k0;k>=min(k0,11);k--) { // Scans backward (decreasing k) until local maximum
                            float sum=0; for(uint i: range(N-k)) sum += signal[i]*signal[k+i]; // N-k0 instead of N-kMax to avoid some doubling
                            debug << str(k, 100*sum)<<", "_;
                            if(sum > maxNCC) maxNCC = sum, kNCC = k, iNCC=6;
                        }
                    }
                }

                int key = floor(pitchToKey(rate/kNCC)); // 8K: floor: +1
                char result = key==expectedKey ? str(iNCC)[0] : '0';

                if(kNCC > 32) { // (+54)
                    int k0 = round(kNCC);
                    for(int k=k0-1;k>0;k--) { // Scans backward (decreasing k) until local maximum to estimate subkey pitch (+19)
                        float sum=0; for(uint i: range(N-k)) sum += signal[i]*signal[k+i];
                        if(sum > maxNCC) maxNCC = sum, kNCC = k;
                        else if(k<k0*63/64) // (16K: +3)
                            break;
                    }
                    // As backward will search regardless of local maximum (if(k<k0*63/64) break; -> +3), compensate with forward local search (+32)
                    for(int k=k0+1;;k++) { // Scans forward (increasing k) until local maximum to estimate subkey pitch
                        float sum=0; for(uint i: range(N-k)) sum += signal[i]*signal[k+i];
                        if(sum > maxNCC) maxNCC = sum, kNCC = k;
                        else break;
                    }
                    int refinedKey = round(pitchToKey(rate/kNCC));
                    if(key!=expectedKey && refinedKey==expectedKey) result = 'R';
                    if(key==expectedKey && refinedKey!=expectedKey) error("Q");
                    key=refinedKey;
                }

                energy[velocityLayer].insert(expectedKey, e);
                if(key==expectedKey) offsets[velocityLayer].insert(expectedKey, 100*12*log2(expectedK/kNCC));
                else {
                    log(expectedK, "kPeak", N/fPeak, "highK", N/highF, "i", iNCC, "k", kNCC,
                        "f/fPeak", expectedF/fPeak, "f/highF", expectedF/highF, "kNCC/k", (float)kNCC/expectedK);
                    if(iNCC==6 && debug) log(debug, 100*maxNCC);
                }
                layerResults << result;
                total++; if(key==expectedKey) success++;
            }
            log(layerResults);
            results << layerResults<<'\n';
        }
        log(success,"/",total); // 16K: 465 / 480, 8K: 472 , 4K: 412 / 464

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
        }*/
        /*if(spectrumPlot) {Plot plot;
            plot.title = String("Spectrum of A6 "_);
            plot.xlabel = String("Frequency"_), plot.ylabel = String("Energy"_);
            plot.dataSets << copy(spectrumPlot);
            //plot.legendPosition = Plot::BottomRight;
            plot.logx=true; // plot.logy=true;
            plots << move(plot);
        }*/
        if(plots) {
            window.backgroundColor=window.backgroundCenter=1;
            window.show();
            window.localShortcut(Escape).connect([]{exit();});
            window.localShortcut(PrintScreen).connect([=]{
                writeFile("energy.png"_, encodePNG(renderToImage(plots[0], int2(1024,768))), home());
                writeFile("output.png"_, encodePNG(renderToImage(plots[1], int2(1024,768))), home());
                writeFile("offset.png"_, encodePNG(renderToImage(plots[2], int2(1024,768))), home());
            });
        }
    }
} test;
