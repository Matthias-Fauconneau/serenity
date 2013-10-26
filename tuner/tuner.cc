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

        const uint N = 8192; // Analysis window size (8K~170ms, 16K~340ms, A-1 (27Hz~2K))
        FFT fft (N);

        const bool singleVelocity = false; // Tests 30 samples or 30x16 samples
        const bool skipHighest = false; // FIXME: Highest sample is quite atonal
        const bool normalize = true;
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
        array<map<float,float>> loudness; // For each note (in MIDI key), loudness relative to average
        loudness.grow(velocityLayers.size);
        array<map<float,float>> offsets; // For each note (in MIDI key), pitch offset (in cents) to equal temperament (A440)
        offsets.grow(velocityLayers.size);
        array<map<float, float>> spectrumPlot;
        spectrumPlot.grow(2);
        float maxAmplitude = 0;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger!=0) continue;
            if(skipHighest && sample.pitch_keycenter >= 108) continue;
            if(singleVelocity) { if(sample.lovel > 64 || 64 > sample.hivel) continue; }
            //else { if(sample.hivel <= 33) continue; }
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
            for(int i: range(N/2)) {
                spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum
                if(i<N*2000./sampleRate && (expectedKey==21+36 || expectedKey==21+36+3) && velocityLayer==8)
                {
                    if(i==0) log(expectedKey, sample.lovel, sample.hivel);
                    spectrumPlot[expectedKey==21+36+3].insert((float)i*sampleRate/N) = spectrum[i];
                }
            }
            float e0=0;
            const uint fMin = N*440*pow(2, -4 - 0./12 - (1./2 / 12))/sampleRate; // ~27 Hz ~ half pitch under A-1
            const uint fMax = N*440*pow(2,  3 + 3./12 + (1./2 / 12))/sampleRate; // ~4308 Hz ~ half pitch over C7
            for(uint i: range(fMin, min(N/2,2*fMax))) // Computes within range where microphone respone is flat
                e0 += spectrum[i] * pow(i, -1./6); // Inverse microphone response*/
            assert(e0);
            float l0=0; //for(uint i: range(N/2)) l0 += loudnessWeight(i)*spectrum[i];
            for(uint i: range(N*50/sampleRate, N*5000/sampleRate)) // Computes within range where microphone respone is flat
                /*e0*/l0 += spectrum[i] * pow(i, -1./6); // Inverse microphone response
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
            } else {
                log(">", expectedKey, expectedK, sample.lovel, sample.hivel);
                log("?", iNCC, maxPeak, kPeak, iNCC*kPeak, maxNCC, kNCC, key);
            }
            if(!normalize) {
                e0 *= sq(sample.volume);
                l0 *= sq(sample.volume);
            }
            energy[velocityLayer].insert(expectedKey, e0);
            loudness[velocityLayer].insert(expectedKey, l0);
            testIndex++;
        }
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
        /*{Plot plot;
            plot.title = String("Pitch ratio (in cents) to equal temperament (A440)"_);
            plot.xlabel = String("Key"_), plot.ylabel = String("Cents"_);
            plot.legends = apply(velocityLayers, [](uint velocity){return str(velocity);});
            plot.dataSets = move(offsets);
            plots << move(plot);
        }*/
        const uint fitBreak = 78;
        {float sy=0, n=0;
            for(const auto& e: energy) for(float y: e.values) n++, sy += y;
            float my = sy / n;
            for(auto& e: energy) for(float& e0: e.values) e0 /= my;
            {float n=0, dyy = 0, max=1, nAll=0, dyyAll = 0;
             for(uint i: range(velocityLayers.size)) for(float key: energy[i].keys) {
                 if(key<=fitBreak) {
                     n++, dyy += sq(log2(energy[i].at(key)));
                     if(abs(log2(energy[i].at(key))>abs(log2(max)))) max = energy[i].at(key);
                 }
                 nAll++, dyyAll += sq(log2(energy[i].at(key)));
             }
             log(sqrt(dyy/n), sqrt(dyyAll/nAll), log2(max)); // Relative deviation
            }
        }
        {float sy=0, n=0;
            for(const auto& e: loudness) for(float y: e.values) n++, sy += y;
            float my = sy / n;
            for(auto& e: loudness) for(float& e0: e.values) e0 /= my;
        }
        array<map<float,float>> normalized = copy(energy);
        //if(normalize) { // Normalize velocity layers
            map<float, float> layerEnergy;
            map<float, float> A, B;
            float mb=0, ma=0;
            for(uint i: range(velocityLayers.size)) { // Fit each layer
                {float n=0, sy=0; // Average energy (constant fit) for low keys
                    for(float key: energy[i].keys) if(key<=fitBreak) n++, sy += energy[i].at(key);
                    layerEnergy.insert(velocityLayers[i].stop, sy/n);
                }

                /*{float n=0, sx=0, sy=0; // linear regression on log2(energy(log2(pitch))) (i.e log2-log2) for high keys
                    for(float key: energy[i].keys) if(key>fitBreak) { // Computes mean x, y
                        float x = log2(keyToPitch(key)), y = log2(energy[i].at(key));
                        n++, sx += x, sy += y;
                    }
                    float mx = sx/n, my = sy/n;
                    float cov=0, var=0;
                    for(float key: energy[i].keys) if(key>fitBreak) { // Computes covariance(x,y) and variance(y)
                        float x = log2(keyToPitch(key)), y = log2(energy[i].at(key));
                        cov += (x-mx)*(y-my);
                        var += sq(x-mx);
                    }
                    float b = cov/var; // slope (log2/log2)
                    float a = my - b*mx; // intercept
                    log(a, b);
                    ma += a; mb += b;
                }*/
                {float n=0, sx=0, sy=0; // linear regression on log2(energy(log2(pitch))) (i.e log2-log2) for high keys
                    for(float key: energy[i].keys) if(key>fitBreak) { // Computes mean x, y
                        float x = log2(keyToPitch(key)), y = log2(energy[i].at(key));
                        n++, sx += x, sy += y;
                    }
                    float mx = sx/n, my = sy/n;
                    float cov=0, var=0;
                    for(float key: energy[i].keys) if(key>fitBreak) { // Computes covariance(x,y) and variance(y)
                        float x = log2(keyToPitch(key)), y = log2(energy[i].at(key));
                        cov += (x-mx)*(y-my);
                        var += sq(x-mx);
                    }
                    float b = cov/var; // slope (log2/log2)
                    float a = my - b*mx; // intercept (linear)
                    ma += a; mb += b;
                    A.insert(velocityLayers[i].stop, a);
                    B.insert(velocityLayers[i].stop, b);
                }
            }
            ma /= velocityLayers.size, mb /= velocityLayers.size;
            log(ma, mb);
            float vMax = layerEnergy.keys.last();
            float eMax = layerEnergy.values.last();
            /*map<float, float> ideal;
            for(uint i: range(velocityCount)) {
                float x = velocityLayers[i].stop;
                ideal.insert(x, sq(x/xMax)*yMax);
                //log(x, sq(x/xMax)*yMax);
            }
            {Plot plot;
                plot.legends << String("Actual"_) << String("Ideal"_);
                plot.dataSets << move(layerEnergy) << move(ideal);
                plots << move(plot);
            }*/
            for(uint i: range(velocityLayers.size)) { // Set each notes to the target energy
                float v = velocityLayers[i].stop;
                float target = sq(v/vMax)*eMax;
                for(float key: normalized[i].keys) if(key<=fitBreak) normalized[i].at(key) = target;
            }
            for(uint i: range(velocityLayers.size)) for(float key: normalized[i].keys) { // Set each notes to the target energy
                float x = keyToPitch(key); float& y = normalized[i].at(key);
                float v = velocityLayers[i].stop;
                float b = mb; //-2; //-2 //B.at(v);
                float flatY = sq(v/vMax)*eMax;
                float a = flatY*pow(keyToPitch(fitBreak),-b); // Override a for C0 continuity with flat part
                if(key>fitBreak) y = a*pow(x,b);
            }
            String sfz;
            // Keys with dampers
            sfz << "<group> ampeg_release=1\n"_;
            float maxGain = 0;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger!=0) continue;
                if(sample.hikey>88) continue;
                int velocityLayer = velocityLayers.indexOf(range(sample.lovel,sample.hivel+1));
                float actual = energy[velocityLayer].at(sample.pitch_keycenter);
                float ideal = normalized[velocityLayer].at(sample.pitch_keycenter);
                maxGain = max(maxGain, sqrt(ideal/actual));
                sfz << "<region> sample="_+sample.name+" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                    +" lovel="_+str(sample.lovel)+" hivel="_+str(sample.hivel)+" pitch_keycenter="_+str(sample.pitch_keycenter)+
                    " volume="_+str(10*log10(ideal/actual))+"\n"_;
            }
            // Keys without dampers
            sfz << "<group> ampeg_release=0\n"_;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger!=0) continue;
                if(sample.hikey<=88) continue;
                int velocityLayer = velocityLayers.indexOf(range(sample.lovel,sample.hivel+1));
                float actual = energy[velocityLayer].at(sample.pitch_keycenter);
                float ideal = normalized[velocityLayer].at(sample.pitch_keycenter);
                maxGain = max(maxGain, sqrt(ideal/actual));
                sfz << "<region> sample="_+sample.name+" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                    +" lovel="_+str(sample.lovel)+" hivel="_+str(sample.hivel)+" pitch_keycenter="_+str(sample.pitch_keycenter)+
                    " volume="_+str(10*log10(ideal/actual))+"\n"_;
            }
            log("maxGain", maxGain, "maxAmplitude", maxAmplitude/(1<<23),
                "headroom", maxGain*maxAmplitude/(1<<23), log2(maxGain*maxAmplitude/(1<<23)));
            // Release samples
            sfz << "<group> trigger=release\n"_;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger==0) continue;
                assert(sample.hikey<=88); // Keys without dampers
                /*int velocityLayer = velocityLayers.indexOf(range(sample.lovel,sample.hivel+1));
                float actual = energy[velocityLayer].at(sample.pitch_keycenter);
                float ideal = normalized[velocityLayer].at(sample.pitch_keycenter);*/
                sfz << "<region> sample="_+sample.name+" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                    +" lovel="_+str(sample.lovel)+" hivel="_+str(sample.hivel)+" pitch_keycenter="_+str(sample.pitch_keycenter)+"\n"_;
                       //+ " volume="_+str(10*log10(ideal/actual))+"\n"_; // Normalized on release by enveloppe
            }
            writeFile("Salamander."_+str(N)+".sfz"_,sfz,Folder("Samples"_));
            /*float totalMean = sum / count; // Average where harmonic energy stays within microphone range
            for(float key: energy[0].keys) {
                float sum = 0, count = 0;
                for(auto& e: energy) sum+=e[key], count++; // Average of one key over all velocity layers
                float keyMean = sum / count;
            }*/
            //TODO: check correction is also coherent with loudness weighting
        //}
            for(auto& e: energy) for(float& k: e.keys) k -= 21; // A0 -> 0
            for(auto& e: normalized) for(float& k: e.keys) k -= 21; // A0 -> 0
            for(auto& e: loudness) for(float& k: e.keys) k -= 21; // A0 -> 0
        {
            for(auto& e: energy) for(float& y: e.values) y = 10*log10(y); // Decibels
            {Plot plot;
                plot.title = String("Energy (fMin, 2*fMax)"_);
                plot.xlabel = String("Key"_), plot.ylabel = String("Decibels"_);
                plot.legends = apply(velocityLayers, [](range velocity){return str(velocity.stop);});
                plot.dataSets = copy(energy);
                //plot.legendPosition = Plot::BottomRight;
                //plot.logx=true, plot.logy=true;
                plots << move(plot);
            }
        }

        /*{
            for(auto& e: normalized) for(float& y: e.values) y = 10*log10(y); // Decibels
            {Plot plot;
                plot.title = String("Normalized"_);
                plot.xlabel = String("Key"_), plot.ylabel = String("Decibels"_);
                plot.legends = apply(velocityLayers, [](range velocity){return str(velocity.stop);});
                plot.dataSets = copy(normalized);
                //plot.legendPosition = Plot::BottomRight;
                //plot.logx=true, plot.logy=true;
                plots << move(plot);
            }
        }*/
        for(auto& e: loudness) for(float& e0: e.values) e0 = 10*log10(e0);
        {Plot plot;
            plot.title = String("Energy (with microphone response correction)"_);
            plot.xlabel = String("Key"_), plot.ylabel = String("Decibels"_);
            plot.legends = apply(velocityLayers, [](range velocity){return str(velocity.stop);});
            plot.legends << apply(velocityLayers, [](range velocity){return str(velocity.stop);});
            plot.dataSets = copy(loudness);
            plot.dataSets << copy(energy);
            plot.legendPosition = Plot::BottomRight;
            plots << move(plot);
        }
            /*{
                {Plot plot;
                    plot.title = String("Spectrum of A2 "_+str(energy[8][36+3]/energy[8][36],loudness[8][36]));
                    plot.xlabel = String("Pitch"_), plot.ylabel = String("Energy"_);
                    plot.dataSets << copy(spectrumPlot[0]);
                    //plot.legendPosition = Plot::BottomRight;
                    //plot.logx=true, plot.logy=true;
                    plots << move(plot);
                }
            }
            {
                {Plot plot;
                    plot.title = String("Spectrum of A2+1 "_+str(energy[8][36+3]/energy[8][36+3],loudness[8][36+3]));
                    plot.xlabel = String("Pitch"_), plot.ylabel = String("Energy"_);
                    plot.dataSets << copy(spectrumPlot[1]);
                    //plot.legendPosition = Plot::BottomRight;
                    //plot.logx=true, plot.logy=true;
                    plots << move(plot);
                }
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
