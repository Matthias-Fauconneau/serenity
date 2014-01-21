#include "thread.h"
#include "map.h"
#include "pitch.h"
#include "ffmpeg.h"
#include <fftw3.h> //fftw3f

/// Estimates pitch and records analysis
struct PitchEstimation {
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

    // Key-specific pitch analysis data
    struct KeyData {
        struct Pitch { float F0, B; };
        array<Pitch> pitch; // Pitch estimation results where this key was detected
        buffer<float> spectrum {N/2, N/2, 0}; // Sum of all power spectrums where this key was detected
    };
    map<int, KeyData> keys;

    PitchEstimation() {
        for(;;) {
            // Input
            buffer<int2> period (periodSize);
            if(audio.read(period) < period.size) { audio.close(); break; }
            for(uint i: range(N-periodSize)) signal[i]=signal[i+periodSize];  //FIXME
            for(uint i: range(periodSize)) signal[N-periodSize+i] = period[i][0] * 0x1p-24; // Left channel only

            // Analysis
            for(uint i: range(N)) estimator.windowed[i] = estimator.window[i] * signal[i];
            float f = estimator.estimate();

            float confidenceThreshold = 1./10; //Relative harmonic energy (i.e over current period energy)
            float ambiguityThreshold = 1./21; // 1- Energy of second candidate relative to first
            float threshold = 1./24;
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
                log(strKey(key)+"\t"_+dec(round(f*rate/N),4)+" Hz\t"_+dec(round(100*offsetF1),2) +" c\t"_);
                KeyData& data = keys[key];
                data.pitch << KeyData::Pitch{estimator.F0, estimator.B};
                assert_(estimator.spectrum.size == N/2);
                for(uint i: range(N/2)) data.spectrum[i] += estimator.spectrum[i];
            }
        }
        // Writes analysis data
        Folder stretch("/var/tmp/stretch"_,root(),true);
        for(int key: keys.keys) {
            const KeyData& data = keys.at(key);
            String f0B; for(KeyData::Pitch pitch: data.pitch) f0B << ftoa(pitch.F0)+" "_+ftoa(pitch.B,2,0,1)+"\n"_;
            writeFile(strKey(key)+".f0B"_, f0B, stretch);
            writeFile(strKey(key)+".PSD"_, cast<byte>(data.spectrum), stretch);
        }
    }
} app;
