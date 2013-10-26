#include "thread.h"
#include "sampler.h"
#include "math.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "png.h"

bool operator ==(range a, range b) { return a.start==b.start && a.stop==b.stop; }

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

/// Estimates fundamental frequency (~pitch) of test samples
struct PitchEstimation {
    VList<Plot> plots;
    Window window {&plots, int2(1024, 768), "Tuner"};
    PitchEstimation() {
        Sampler sampler;
        const uint sampleRate = 48000;
        sampler.open(sampleRate, "Salamander.sfz"_, Folder("Samples"_,root()));

        const uint N = 4096; // Analysis window size (A-1 (27Hz~2K))
        FFT fft (N);

        const bool singleVelocity = false; // Tests 30 samples or 30x16 samples
        const bool skipHighest = false; // FIXME: Highest sample is quite atonal
        uint tests = 0;
        array<range> velocityLayers;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(skipHighest && sample.pitch_keycenter >= 108) continue;
            if(singleVelocity) { if(sample.lovel > 64 || 64 > sample.hivel) continue; }
            //else { if(sample.hivel <= 33) continue; }
            if(!velocityLayers.contains(range(sample.lovel,sample.hivel+1)))
                velocityLayers << range(sample.lovel,sample.hivel+1);
            tests++;
        }

        uint result[tests]; // Rank of actual pitch within estimated candidates
        clear(result, tests, uint(~0));
        uint testIndex=0;
        array<map<float,float>> energy; // For each note (in MIDI key), energy relative to average
        energy.grow(velocityLayers.size);
        array<map<float,float>> offsets; // For each note (in MIDI key), pitch offset (in cents) to equal temperament (A440)
        offsets.grow(velocityLayers.size);
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(skipHighest && sample.pitch_keycenter >= 108) continue;
            if(singleVelocity) { if(sample.lovel > 64 || 64 > sample.hivel) continue; }
            int expectedKey = sample.pitch_keycenter;
            int velocityLayer = velocityLayers.indexOf(range(sample.lovel,sample.hivel+1));

            assert(N<=sample.flac.duration);
            buffer<float2> stereo = decodeAudio(sample.data, N);
            float signal[N];
            for(uint i: range(N)) {
                maxAmplitude = max(maxAmplitude, max(abs(stereo[i][0]),abs(stereo[i][1])));
                signal[i] = (stereo[i][0]+stereo[i][1])/(2<<(24+1));
            }
            ref<float> halfcomplex = fft.transform(signal);
            buffer<float> spectrum (N/2);
            float e = 0;
            for(int i: range(N/2)) {
                spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum
                e += spectrum[i];
            }
            const uint fMin = N*440*pow(2, -4 - 0./12 - (1./2 / 12))/sampleRate; // ~27 Hz ~ half pitch under A-1
            const uint fMax = N*440*pow(2,  3 + 3./12 + (1./2 / 12))/sampleRate; // ~4308 Hz ~ half pitch over C7
            // Estimates candidates using maximum peak
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
                offsets[velocityLayer].insert(expectedKey, 100*12*log2(expectedK/kNCC));
                //if(iNCC) log(iNCC, kNCC-int(iNCC*kPeak), kNCC/(iNCC*kPeak));
            } /*else {
                log(">", expectedKey, expectedK, sample.lovel, sample.hivel);
                log("?", iNCC, maxPeak, kPeak, iNCC*kPeak, maxNCC, kNCC, key);
            }*/
            energy[velocityLayer].insert(expectedKey, e);
            testIndex++;
        }

        /*// Pitch estimation report
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
        log(detail, "("_+s+")/"_,tests); // 16K: 453; 32K: 455 / 464*/

        const float e0 = mean(energy.last().values); // Computes mean energy of highest velocity layer
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
        }

        {
            for(auto& e: energy) for(float& k: e.keys) k -= 21; // A0 -> 0
            for(auto& e: energy) for(float& y: e.values) y = 10*log10(y); // Decibels
            {Plot plot;
                plot.title = String("Energy"_);
                plot.xlabel = String("Key"_), plot.ylabel = String("Decibels"_);
                plot.legends = apply(velocityLayers, [](range velocity){return str(velocity.stop);});
                plot.dataSets = copy(energy);
                plots << move(plot);
            }
        }
        /*{Plot plot;
            plot.title = String("Pitch ratio (in cents) to equal temperament (A440)"_);
            plot.xlabel = String("Key"_), plot.ylabel = String("Cents"_);
            plot.legends = apply(velocityLayers, [](uint velocity){return str(velocity);});
            plot.dataSets = move(offsets);
            plots << move(plot);
        }*/
        window.backgroundColor=window.backgroundCenter=1;
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(PrintScreen).connect([=]{
            writeFile("energy.png"_, encodePNG(renderToImage(plots[0], int2(1024,768))), home());
            writeFile("output.png"_, encodePNG(renderToImage(plots[1], int2(1024,768))), home());
            writeFile("offset.png"_, encodePNG(renderToImage(plots[2], int2(1024,768))), home());
        });
        //window.localShortcut(PrintScreen)();
    }
} test;
