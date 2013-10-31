#include "thread.h"
#include "pitch.h"
#include "sampler.h"
#include "math.h"
#include "time.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "png.h"

bool operator ==(range a, range b) { return a.start==b.start && a.stop==b.stop; }

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline float loudnessWeight(float f) {
    const float a = sq(20.6), b=sq(107.7), c=sq(737.9), d=sq(12200);
    float f2 = f*f;
    return d*f2*f2 / ((f2 + a) * sqrt((f2+b)*(f2+c)) * (f2+d));
}

/// Estimates loudness fundamental frequency (~pitch) of samples. Estimates pitch shifts to minimize total entropy.
struct Tuner {
    VList<Plot> plots;
    Window window {&plots, int2(1024, 768), "Tuner"};
    Tuner() {
        Sampler sampler;
        const uint rate = 48000;
        sampler.open(rate, "Salamander.original.sfz"_, Folder("Samples"_,root()));

        array<range> velocityLayers; array<int> keys;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(sample.pitch_keycenter == 108) continue;
            if(!velocityLayers.contains(range(sample.lovel,sample.hivel+1))) velocityLayers << range(sample.lovel,sample.hivel+1);
            if(!keys.contains(sample.pitch_keycenter)) keys << sample.pitch_keycenter;
        }

        const uint N = 8192; // Analysis window size (A-1 (27Hz~2K))
        const int M = 8*12*100; //N;//log2(N/2)*256; // 12 octaves * 1024 bins/octaves (=85 bins / key (1.2 cents resolution))
        PitchEstimator pitchEstimator (N);

        Random random;
        map<float, float> spectrumPlot;
        array<buffer<real>> logSpectrums; // For each note (in MIDI key), spectrum of loudness in logarithmic bins
        int tune[keys.size]; clear(tune, keys.size);
        array<map<float,float>> offsets; offsets.grow(velocityLayers.size); // For each note (in MIDI key), pitch offset (in cents) to equal temperament
        array<map<float,float>> energy; energy.grow(velocityLayers.size); // For each note (in MIDI key), energy relative to average
#define BENCHMARK 1 // Benchmarks pitch estimation or compute log spectrums for entropy tuning
#if BENCHMARK
        uint success = 0, total = 0;
        String results;
        for(int velocityLayer: range(velocityLayers.size)) {
            String layerResults;
#else
        int velocityLayer = 8;
#endif
        for(uint keyIndex: range(keys.size)) {
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

            assert(N<=sample.flac.duration);
            buffer<float2> stereo = decodeAudio(sample.data, N);
            float signal[N]; for(uint i: range(N)) signal[i] = (stereo[i][0]+stereo[i][1]) * 0x1p-25f;
            float k = pitchEstimator.estimate(signal);
            int key = round(pitchToKey(rate/k));
            int expectedKey = sample.pitch_keycenter;
            float expectedK = rate/keyToPitch(expectedKey);

            //energy[velocityLayer].insert(expectedKey, e); FIXME
            if(key==expectedKey) offsets[velocityLayer].insert(keyIndex, 100*12*log2(expectedK/k));
#if BENCHMARK
            else log(expectedK, "k",k, "e", k/expectedK);
            layerResults << (key==expectedKey ? '0' : 'X');
            total++; if(key==expectedKey) success++;
        }
        log(layerResults);
        results << layerResults<<'\n';
    }
    log(success,"/",total); // 8K: 464 / 464, 472 / 480
#else
            (void)result;
            if(key==expectedKey) {// Computes logarithmic spectrum
                const uint fMin = 4; //1; //~ 6 Hz (rate/N)
                const uint fMax = N/8; // /N/2; //~ 24000 Hz (rate/2)
                buffer<real> logSpectrum(M); clear(logSpectrum.begin(), M);
                int offset = round(100*12*log2(expectedK/kNCC)); tune[keyIndex] = offset;
                for(int m: range(M)) {
                    real n0 = fMin*pow(2, (real)m/M*log2(fMax/fMin));
                    real n1 = fMin*pow(2, (real)(m+1)/M*log2(fMax/fMin));
                    assert_(floor(n0)<ceil(n1));
                    real sum = 0;
                    //for(uint n: range(floor(n0),ceil(n1))) sum += loudnessWeight((float)n*rate/N) * spectrum[n];
                    sum += (ceil(n0)-n0) * loudnessWeight(n0*rate/N) * spectrum[n0];
                    for(uint n: range(ceil(n0),floor(n1))) sum += loudnessWeight((float)n*rate/N) * spectrum[n];
                    if(n1 < N/2) sum += (n1-floor(n1)) * loudnessWeight(n1*rate/N) * spectrum[n1];
                    assert(sum >= 0);
                    if(m+offset>=0 && m+offset<M) logSpectrum[m+offset] = sum;
                }
                logSpectrums << move(logSpectrum);
            }
        }
#endif
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

        if(logSpectrums) { // Entropy-based tuning (for efficiency, spectrums are not reweighted with the correct loudness on each shift)
            assert_(logSpectrums.size == keys.size);
            const int keyCents = (M/log2(N/2))/12; // Number of bins ("cents") for a tone (key)
            log((float)(M/8)/12, (float)(M-2*keyCents)/M);
            // Computes initial sum of all spectrums
            buffer<real> total(M); clear(total.begin(), total.size);
            real totalEnergy = 0;
            for(int key: range(keys.size)) {
                real previous = 0;
                real* s = logSpectrums[key].begin();
                for(int m: range(M)) {
                    real current = s[m];
                    if(m < keyCents || m >= M-keyCents) current = 0; // Trim spectrum to avoid window boundary effects (FIXME)
                    total[m] += current;
                    totalEnergy += current;
                    s[m] = current - previous; // Converts spectrum to left shift operator
                    previous = current;
                }
            }
            assert_(totalEnergy);
            for(int m: range(M)) total[m] /= totalEnergy; // Normalizes (for entropy evaluation)
            for(int key: range(keys.size)) {
                real* s = logSpectrums[key].begin();
                for(int m: range(M)) s[m] /= totalEnergy; // Normalizes shifts operators also
            }
            int shiftCount[keys.size]; clear(shiftCount, keys.size); // Positive offset is lower pitch (left shifts)
            //Random random;
            real entropy = 0;
            for(int m: range(M)) if(total[m]>0) entropy += total[m] * log2(total[m]); entropy=-entropy;
            for(uint tries=0;tries<4*88;tries++) {
                int key = random%keys.size; // or in order ?
                int delta = random%2 ? 1 : -1; // and try both
                //TODO: record all candidate configurations to avoid testing same configuration twice (and search diamond exhaustively)
                int offset = shiftCount[key]; // Value
                real currentEnergy = 0; real candidateEntropy = 0;
                if(delta>0) offset++;
                assert_(offset < keyCents, offset, keyCents); // Prevents windowing effects
                const real* shift = logSpectrums[key] + offset;
                assert_(shift[-offset]==0 && shift[M-offset-1]==0, offset);
                real shiftGain = 0;
                for(int m: range(0, max(0,-offset))) assert_(total[m]==0);
                for(int m: range(max(0,-offset), M+min(0,-offset))) {
                    assert_(m>=0 && m<M && offset+m>=0 && offset+m<M);
                    real v = total[m] + delta * shift[m];
                    shiftGain += shift[m];
                    currentEnergy += v;
                    assert_(v>=-0x1p-60, log2(-v));
                    if(v>0) candidateEntropy += v * log2(v);
                }
                for(int m: range(M+min(0,-offset), M)) assert_(total[m]==0);
                assert_(abs(shiftGain)<0x1p-29, log2(abs(shiftGain)));
                assert_(total[0]==0 && total[M-1]==0);
                assert_(abs(1-currentEnergy) < 0x1p-26, log2(abs(1-currentEnergy))); // No need to renormalize as there is no windowing
                candidateEntropy = -candidateEntropy;
                if(candidateEntropy < entropy/**(1-0x1p-13)*/) { // Commits the change
                    int& offset = shiftCount[key]; // Reference this time
                    if(delta>0) offset++;
                    const real* shift = logSpectrums[key] + offset;
                    for(int m: range(max(0,-offset), M+min(0,-offset))) total[m] += delta * shift[m];
                    if(delta<0) offset--;
                    tune[key] -= delta;
                    log(delta>0?"+":"-", key, offset-delta,"->",offset, "\t", tune[key]+delta,"->",tune[key], "\t", entropy,"->",candidateEntropy, "["_+str(tries)+"]"_);
                    entropy = candidateEntropy;
                    tries = 0; // Resets counter
                } //else if(entropy<candidateEntropy) log(log2(1-entropy/candidateEntropy));
                assert(entropy);
            }
            for(int key: range(keys.size)) offsets[0].insert(key, offsets[8].at(key)-tune[key]);
        }

        if(0) {Plot plot;
            plot.title = String("Pitch ratio (in cents) to equal temperament (A440)"_);
            plot.xlabel = String("Key"_), plot.ylabel = String("Cents"_);
            plot.legends = apply(velocityLayers, [](range velocity){return str(velocity.stop);});
            plot.dataSets = move(offsets);
            plots << move(plot);
        }
        if(0) {
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
        if(spectrumPlot) {Plot plot;
            plot.title = String("Log spectrum"_);
            plot.xlabel = String("Frequency"_), plot.ylabel = String("Energy"_);
            plot.dataSets << copy(spectrumPlot);
            //plot.legendPosition = Plot::BottomRight;
            //plot.logx=true; // plot.logy=true;
            plots << move(plot);
        }
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
