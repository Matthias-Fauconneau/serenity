#include "thread.h"
#include "math.h"
#include "pitch.h"
#include "ffmpeg.h"
#include "data.h"
#include "time.h"
#include "layout.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include <fftw3.h> //fftw3f

int parseKey(TextData& s) {
    int key=24;
    if(!s.matchAny("cdefgabCDEFGAB"_)) return -1;
    key += "c#d#ef#g#a#b"_.indexOf(toLower(s.next()));
    if(s.match('#')) key++;
    key += 12*s.mayInteger(4);
    return key;
}

uint localMaximum(ref<float> spectrum, uint start) {
    uint best=start; float maximum=spectrum[start];
    for(int i=start+1; i<int(spectrum.size); i++) {
        if(spectrum[i]<maximum) break;
        maximum = spectrum[i], best=i;
    }
    for(int i=start-1; i>=0; i--) {
        if(spectrum[i]<maximum) break;
        maximum = spectrum[i], best=i;
    }
    return best;
}

struct StretchEstimation : Poll, Widget {
    // Input
    //static constexpr uint rate = 96000;
    //const string path = "/Samples"_;
    static constexpr uint rate = 48000;
    //const string path = "/Samples/Blanchet"_;
    const string path = "/Samples/Salamander"_;
    array<String> samples = Folder(path).list(Files);
    AudioFile file;
    buffer<float> signal {N,N,0};

    // Analysis
    static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist))
    static constexpr uint periodSize = 4096;
    PitchEstimator estimator {N};

    // UI
    Window window {this, int2(1050, 1680/2), "Stretch"_};

    static constexpr int keyCount = 85;
    array<float> F1[keyCount];
    array<float> F2[keyCount];

    //float stretch(int key) { return 0; }
    float stretch(int key) { return 1.f/32 * (float)(key - keyCount/2) / 12; }

    StretchEstimation() {
        window.backgroundColor=window.backgroundCenter=0; additiveBlend = true;
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
        queue();
    }
    void event() {
        for(;;) {
            // Input
            buffer<int2> period (periodSize);
            while(!file || file.read(period) < period.size) {
                file.close();
                while(!file) {
                    if(!samples) return;
                    TextData s = samples.pop();
                    if(endsWith(s.buffer,".flac"_))
                        //if(parseKey(s)>=0 && s.match("-"_) && parseKey(s)>=0)
                        file.openPath(path+"/"_+s.buffer);
                }
                assert_(file.rate==rate);
            }
            for(uint i: range(N-periodSize)) signal[i]=signal[i+periodSize];  //FIXME
            for(uint i: range(periodSize)) signal[N-periodSize+i] = period[i][0] * 0x1p-24; // Left channel only

            // Analysis
            for(uint i: range(N)) estimator.windowed[i] = estimator.window[i] * signal[i];
            float f = estimator.estimate();

            float confidenceThreshold = 1./10; //9-10 Relative harmonic energy (i.e over current period energy)
            float ambiguityThreshold = 1./21; // 1- Energy of second candidate relative to first
            float threshold = 1./24; // 19-24
            float offsetThreshold = 1./2;
            if(f < 13) { // Strict threshold for ambiguous bass notes
                threshold = 1./21;
                offsetThreshold = 0.43;
            }

            float confidence = estimator.harmonicEnergy  / estimator.periodEnergy;
            float ambiguity = estimator.candidates.size==2 && estimator.candidates[1].key
                    && estimator.candidates[0].f0*(1+estimator.candidates[0].B)!=f ?
                        estimator.candidates[0].key / estimator.candidates[1].key : 0;

            int key = f > 0 ? round(pitchToKey(f*rate/N)) : 0;
            float keyF0 = f > 0 ? keyToPitch(key)*N/rate : 0;
            const float offsetF1 = f > 0 ? 12*log2(f/keyF0) : 0;

            if(confidence > confidenceThreshold && 1-ambiguity > ambiguityThreshold && confidence*(1-ambiguity) > threshold
                    && abs(offsetF1)<offsetThreshold) {
                float f1 = f;
                float f2 = estimator.F0*(2+estimator.B*cb(2));
#if 0 // Overrides least square fit with a direct estimation from spectrum peaks
                ref<float> spectrum = estimator.filteredSpectrum;
                f1 = localMaximum(spectrum, round(f1));
                f2 = localMaximum(spectrum, round(f2));
#endif
#if 0 // Correct mistune to only estimate inharmonicity (incorrect in presence of resonnance or tuning dependant inharmonicity)
                float target = keyF0*exp2(stretch(key-21)/12);
                f1 = target;
                f2 = target*(2+estimator.B*cb(2));
#endif
                log(strKey(key)+"\t"_+dec(round(f*rate/N),4)+" Hz\t"_+dec(round(100*offsetF1),2) +" c\t"_+dec(round(100*12*log2(f2/(2*f1))))+" c\t"_);
                if(key>=21 && key<21+keyCount) {
                    F1[key-21] << f1;
                    F2[key-21] << f2;
                }
                break;
            }
        }
        window.render();
    }
    void render(int2 position, int2 size) {
        for(uint key: range(keyCount)) {
            uint x0 = position.x + key * size.x / keyCount;
            uint x1 = position.x + (key+1) * size.x / keyCount;
            float keyF0 = keyToPitch(key+21)*N/rate;
            if(F1[key]) {
                float sum = 0; uint count = F1[key].size;
                for(float f: F1[key]) {
                    float offset = 12*log2(f/keyF0);
                    int y = position.y + size.y/2 - offset * size.y;
                    line(int2(x0,y), int2(x1,y), vec4(0,1,0,1./count));
                    sum += f;
                }
                float mean = sum / count;
                float offset = 12*log2(mean/keyF0);
                float y = position.y + size.y/2 - offset * size.y;
                line(x0,y, x1,y, vec4(0,1,0,1));
            }
            if(key >= 12 && F2[key-12]) {
                float sum = 0; uint count = F1[key-12].size;
                for(float f: F2[key-12]) {
                    float offset = 12*log2(f/keyF0);
                    int y = position.y + size.y/2 - offset * size.y;
                    line(int2(x0,y), int2(x1,y), vec4(0,0,1,1./count));
                    sum += f;
                }
                float mean = sum / count;
                float offset = 12*log2(mean/keyF0);
                float y = position.y + size.y/2 - offset * size.y;
                line(x0,y, x1,y, vec4(0,0,1,1));
            }
        }
        if(file) queue();
    }
} app;

