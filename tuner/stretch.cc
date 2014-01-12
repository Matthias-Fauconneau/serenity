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

struct StretchEstimation : Widget {
    // Analysis
    static constexpr uint rate = 96000;
    static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist))
    static constexpr uint periodSize = 4096;
    PitchEstimator estimator {N};

    // UI
    Window window {this, int2(1050, 1680/2), "Stretch"_};

    static constexpr int keyCount = 85;
    array<uint> F1[keyCount];
    array<uint> F2[keyCount];

    StretchEstimation() {
        window.backgroundColor=window.backgroundCenter=0; additiveBlend = true;
        window.localShortcut(Escape).connect([]{exit();});

        for(uint i: range(/*1*/4,6)) {
            buffer<float> signal {N};
            Audio audio = decodeAudio("/Samples/A"_+str(i)+"-A"_+str(i+1)+".flac"_); //FIXME
            assert_(audio.rate==rate);
            for(uint t=0; t<=audio.size-periodSize; t+=periodSize) {
                // Prepares new period
                const ref<int2> period = audio.slice(t, periodSize);
                for(uint i: range(N-periodSize)) signal[i]=signal[i+periodSize];  //FIXME
                for(uint i: range(periodSize)) signal[N-periodSize+i] = period[i][0] * 0x1p-24; // Left channel only
                if(t<5*rate) continue; // First 5 seconds are silence (let input settle, use for noise profile if necessary)
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

                ref<float> spectrum = estimator.filteredSpectrum;
                float confidence = estimator.harmonicEnergy  / estimator.periodEnergy;
                float ambiguity = estimator.candidates.size==2 && estimator.candidates[1].key
                        && estimator.candidates[0].f0*(1+estimator.candidates[0].B)!=f ?
                            estimator.candidates[0].key / estimator.candidates[1].key : 0;

                int key = f > 0 ? round(pitchToKey(f*rate/N)) : 0;
                float keyF0 = f > 0 ? keyToPitch(key)*N/rate : 0;
                const float offsetF0 = f > 0 ? 12*log2(f/keyF0) : 0;

                if(confidence > confidenceThreshold && 1-ambiguity > ambiguityThreshold && confidence*(1-ambiguity) > threshold
                        && abs(offsetF0)<offsetThreshold) {
                        uint f1 = localMaximum(spectrum, round(f));
                        F1[key-21] << f1;
                        uint f2 = localMaximum(spectrum, 2*f1);
                        F2[key-21] << f2;
                        log(strKey(key)+"\t"_+dec(round(f*rate/N),4)+" Hz\t"_+dec(round(100*offsetF0),2) +" c\t"_+str(f1)+" "_+str(f2)+"\t"_
                            +str(12*log2(f1/keyF0))+" "_+str(12*log2(f2/(2*keyF0))));
                }
            }
        }
        window.show();
    }
    void render(int2 position, int2 size) {
        for(uint key: range(keyCount)) {
            uint x0 = position.x + key * size.x / keyCount;
            uint x1 = position.x + (key+1) * size.x / keyCount;
            float keyF0 = keyToPitch(key+21)*N/rate;
            for(uint f: F1[key]) {
                float offset = 12*log2(f/keyF0);
                int y = position.y + (1+offset) * size.y/2;
                line(x0,y,x1,y, green);
            }
            if(key >= 12) for(uint f: F2[key-12]) {
                float offset = 12*log2(f/keyF0);
                int y = position.y + (1+offset) * size.y/2;
                line(x0,y,x1,y, red);
            }
        }
    }
} app;

