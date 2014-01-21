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

float mean(const ref<float>& v) { return v.size ? sum(v)/v.size : 0; }

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
    struct Pitch { float F0, B; };
    array<Pitch> pitch[keyCount];
    buffer<float> spectrum[keyCount];

    float stretch(int key) { return
                1.2/100 * exp2((key-(39+12))/8.) // Treble inharmonicity
              - 1.2/100 * exp2(-(key-(26))/8.); // Bass inharmonicity
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
                ref<float> spectrum = estimator.filteredSpectrum;
                log(strKey(key)+"\t"_+dec(round(f*rate/N),4)+" Hz\t"_+dec(round(100*offsetF1),2) +" c\t"_);
                if(key>=21 && key<21+keyCount) {
                    pitch[key-21] << Pitch{estimator.F0, estimator.B};
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
            const float scale = 4;
            auto render = [&](uint key, float harmonicRatio, vec3 color) {
                ref<float> power = spectrum[key];
                float meanPower = mean(power);
                ref<Pitch> s = pitch[key];
                float meanF1; {float sum = 0; for(Pitch p: s) sum += p.F0*(1+p.B); meanF1 = sum / s.size;}
                for(uint n: range(1, 3 +1)) {
                    int N = harmonicRatio*n;
                    int y0 = (3-n)*size.y/3, y1 = (3-n+1)*size.y/3, y12 = (y0+y1)/2;
                    float maxPower = meanPower;
                    for(uint f: range(1, power.size)) {
                        float y = y12 - log2((float) f / N / meanF1 * exp2(stretch(key)/12)) * size.y;
                        if(y>y0 && y<y1) maxPower = max(maxPower, power[f]);
                    }
                    float sum = 0;
                    for(Pitch p: s) {
                        float f = p.F0*(N+p.B*cb(N));
                        float y = y12 - log2(f/N /meanF1 * exp2(stretch(key)/12)) * size.y * scale;
                        if(y>y0 && y<y1 && round(f)<power.size) {
                            float p = power[round(f)];
                            float intensity = (p  > meanPower ? log2(p / meanPower) / log2(maxPower / meanPower) : 0) / s.size;
                            line(int2(x0, position.y + y), int2(x1, position.y + y), vec4(color,intensity));
                        }
                        sum += f;
                    }
                    float f = sum / s.size;
                    float y = y12 - log2(f/N /meanF1 * exp2(stretch(key)/12)) * size.y * scale;
                    if(y>y0 && y<y1 && round(f)<power.size) {
                        float p = power[round(f)];
                        float intensity = p  > meanPower ? log2(p / meanPower) / log2(maxPower / meanPower) : 0;
                        line(x0, position.y + y, x1, position.y + y, vec4(color,intensity));
                    }
                }
            };
            if(pitch[key]) render(key, 1, vec3(0,1,0));
            if(key >= 12 && pitch[key-12])  render(key-12, 2, vec3(1,0,1));
        }
        if(audio) queue();
    }
} app;

