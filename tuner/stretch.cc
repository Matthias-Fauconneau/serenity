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

/// Visualizes data generated by \a PitchEstimation
struct StretchEstimation : Widget {
    // UI
    Window window {this, int2(0, 0), "Stretch"_};

    // Key-specific pitch analysis data
    struct KeyData {
        struct Pitch { float F0, B; };
        array<Pitch> pitch; // Pitch estimation results where this key was detected
        buffer<float> spectrum; // Sum of all power spectrums where this key was detected
    };
    map<int, KeyData> keys;

    float sB = 33, sT = 150;
    float& variable = sT;

    StretchEstimation() {
        // Reads analysis data
        Folder stretch("/var/tmp/stretch"_);
        for(string file: stretch.list(Files)) {
            if(!endsWith(file, "f0B"_)) continue;
            string name = section(file,'.');
            KeyData data;
            data.pitch = cast<KeyData::Pitch>(readFile(name+".f0B"_, stretch));
            data.spectrum = cast<float>(readFile(name+".PSD"_, stretch));
            static constexpr uint N = 32768;
            assert(data.spectrum.size == N/2);
            keys.insert(parseKey(name), move(data));
        }

        window.backgroundColor=window.backgroundCenter=0; additiveBlend = true;
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(KP_Sub).connect([this]{variable--; window.render(); window.setTitle(str(variable));});
        window.localShortcut(KP_Add).connect([this]{variable++; window.render(); window.setTitle(str(variable));});
        window.show();
    }

    //-exp(8.9e-2 * (-30 - m)) + exp(9.44e-2 * (m - 147));
    //float stretch(int m) { return -exp((-33 - m)/12.) + exp((m - 150)/12.); }
    float stretch(int m) { return -exp((-sB - m)/12.) + exp((m - sT)/12.); }

    void render(int2 position, int2 size) {
        range compass(keys.keys.first(), keys.keys.last());
        for(int key: compass) {
            uint x0 = position.x + (key       - compass.start) * size.x / (compass.stop - compass.start);
            uint x1 = position.x + (key +1 - compass.start) * size.x / (compass.stop - compass.start);
            const float scale = 4;
            auto render = [&](int key, float harmonicRatio, vec3 color) {
                if(!keys.contains(key)) return;
                ref<float> power = keys.at(key).spectrum;
                float meanPower = sum(power) / power.size;
                ref<KeyData::Pitch> s = keys.at(key).pitch;
                float meanF1; {float sum = 0; for(KeyData::Pitch p: s) sum += p.F0*(1+p.B); meanF1 = sum / s.size;}
                for(uint n: range(1, 3 +1)) {
                    int N = harmonicRatio*n;
                    int y0 = (3-n)*size.y/3, y1 = (3-n+1)*size.y/3, y12 = (y0+y1)/2;
                    float maxPower = meanPower;
                    for(uint f: range(1, power.size)) {
                        float y = y12 - log2((float) f / N / meanF1 * exp2(stretch(key))) * size.y;
                        if(y>y0 && y<y1) maxPower = max(maxPower, power[f]);
                    }
                    float sum = 0;
                    for(KeyData::Pitch p: s) {
                        float f = p.F0*(N+p.B*cb(N));
                        float y = y12 - log2(f/N /meanF1 * exp2(stretch(key))) * size.y * scale;
                        if(y>y0 && y<y1 && round(f)<power.size) {
                            float p = power[round(f)];
                            float intensity = (p  > meanPower ? log2(p / meanPower) / log2(maxPower / meanPower) : 0) / s.size;
                            line(int2(x0, position.y + y), int2(x1, position.y + y), vec4(color,intensity));
                        }
                        sum += f;
                    }
                    float f = sum / s.size;
                    float y = y12 - log2(f/N /meanF1 * exp2(stretch(key))) * size.y * scale;
                    if(y>y0 && y<y1 && round(f)<power.size) {
                        float p = power[round(f)];
                        float intensity = p  > meanPower ? log2(p / meanPower) / log2(maxPower / meanPower) : 0;
                        line(x0, position.y + y, x1, position.y + y, vec4(color,intensity));
                    }
                }
            };
            render(key, 1, vec3(0,1,0));
            render(key-12, 2, vec3(1,0,1));
        }
    }
} app;

