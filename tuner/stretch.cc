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
    if(start>=spectrum.size) return start;
    float maximum=spectrum[start]; uint best=start;
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
    const uint lowKey=parseKey(arguments().value(0,"A0"))-12, highKey=parseKey(arguments().value(1,"A7"_))-12;
    AudioFile audio {"/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_};
    const uint rate = audio.rate;
    uint t=0;

    // Analysis
    static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist))
    static constexpr uint periodSize = 4096;
    buffer<float> signal {N};
    PitchEstimator estimator {N};
    int expectedKey = highKey+1;

    // UI
    Window window {this, int2(0, 0), "Stretch"_};

    static constexpr int keyCount = 85;
    array<float> F1[keyCount];
    array<float> F2[keyCount];
    buffer<float> spectrum[keyCount];

    //float stretch(int key) { return 0; }
    //float stretch(int key) { return 1.f/32 * (float)(key - keyCount/2) / 12; }
    float stretch(int key) { return
                1.2/100 * exp2((key-(39+12))/8.) // Treble inharmonicity (1./64?)
                - 1./256 * exp2(-(key-(26))/8.); // Bass inharmonicity
                           }

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
            if(audio.read(period) < period.size) { audio.close(); break; }
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
                ref<float> spectrum = estimator.filteredSpectrum;
#if 0 // Overrides least square fit with a direct estimation from spectrum peaks
                f1 = localMaximum(spectrum, round(f1));
                f2 = localMaximum(spectrum, round(f2));
#endif
#if 1 // Correct mistune to only estimate inharmonicity (incorrect in presence of resonnance or tuning dependant inharmonicity)
                float target = keyF0*exp2(stretch(key-21)/12);
                if(f1) f2 *= target/f1;
                f1 = target;
#endif
                log(strKey(key)+"\t"_+dec(round(f*rate/N),4)+" Hz\t"_+dec(round(100*offsetF1),2) +" c\t"_+dec(round(100*12*log2(f2/(2*f1))))+" c\t"_);
                if(key>=21 && key<21+keyCount) {
                    F1[key-21] << f1;
                    F2[key-21] << f2;
                    if(!this->spectrum[key-21]) this->spectrum[key-21] = buffer<float>(spectrum.size, spectrum.size, 0);
                    for(uint i: range(spectrum.size)) this->spectrum[key-21][i] += spectrum[i];
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
            //float equalTemperament = keyToPitch(key+21)*N/rate;
            float firstPartial = F1[key].size ? sum(F1[key]) / F1[key].size : 0;
            float octaveFirstPartial = (key>=12 && F1[key-12].size) ? sum(F1[key-12]) / F1[key-12].size : 0;
            buffer<float> current (size.y, size.y, 0);
            buffer<float> octave (size.y, size.y, 0);
            for(uint f: range(1, spectrum[key].size)) { // Resample spectrum to log scale
                //float offset = /*12**/log2(f/equalTemperament/*firstPartial*/);
                //int y = size.y - round(offset * size.y);
                {
                    //int y = size.y*3/4/2 - (f - firstPartial); // Normalize tuning offset
                    //int y = size.y/2 - (f - firstPartial); // Normalize tuning offset
                    int y = size.y*3/4 - log2(f / firstPartial * exp2(stretch(key)/12)) * size.y/2; // Normalize tuning offset
                    if(y>=0 && y<size.y) current[y] += spectrum[key][f];
                }
                if(key>=12 && spectrum[key-12]) {
                    //int y = size.y*3/4/2 - (f - firstPartial/*octaveFirstPartial*2*/); // Normalize tuning offset + ET tuning ratio
                    //int y = size.y/2 - (f - firstPartial); // Normalize tuning offset + ET tuning ratio
                    //int y = size.y/2 - log2(f / firstPartial) * size.y/3; // Normalize tuning offset
                    int y = size.y*3/4 - log2(f / (octaveFirstPartial*2) * exp2(stretch(key-12)/12)) * size.y/2; // Normalize tuning offset
                    if(y>=0 && y<size.y) octave[y] += spectrum[key-12][f];
                }
            }

            float currentMean = sum(current) / current.size, octaveMean = sum(octave) / octave.size;
            float currentMax = ::max(current), octaveMax = ::max(octave/*.slice(0,octave.size*1/4)*/);
            if(!currentMax || !octaveMax) continue;
            for(uint y: range(size.y)) {
                //current[y] / currentMax * 0xFF
                float c = currentMean && currentMax && current[y]>currentMean ? log2(current[y] / currentMean) / log2(currentMax / currentMean) * 0xFF : 0;
                float o = octaveMean && octaveMax && octave[y]>octaveMean ? log2(octave[y] / octaveMean) / log2(octaveMax / octaveMean) * 0xFF : 0;
                byte4 color (clip<int>(0,round(c),0xFF), clip<int>(0,round(o),0xFF), clip<int>(0,round(c),0xFF), 0xFF);
                //for(uint x: range(x0, x1)) { framebuffer(x,y*2) = c; framebuffer(x,y*2+1) = c; }
                for(uint x: range(x0, x1)) framebuffer(x,y) = color;
            }
            /*if(F1[key]) {
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
            }*/
        }
        if(audio) queue();
    }
} app;

