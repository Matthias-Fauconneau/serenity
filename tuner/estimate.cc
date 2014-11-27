#include "thread.h"
#include "map.h"
#include "pitch.h"
#include "audio.h"
#include <fftw3.h> //fftw3f

/// Estimates pitch and records analysis
struct PitchEstimation {
    // Input
	AudioFile audio {"/Samples/Klavier.mkv"_};
	const uint rate = audio.audioFrameRate;
    uint t=0;

    // Analysis
    static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist)) TODO: 64K resolution
    static constexpr uint periodSize = 4096;
    buffer<float> signal {N};
    PitchEstimator estimator {N};

    // Key-specific pitch analysis data
    struct KeyData {
        struct Pitch { float F0, B; };
        array<Pitch> pitch; // Pitch estimation results where this key was detected
		buffer<float> spectrum {N/2}; // Sum of all power spectrums where this key was detected
		KeyData() { spectrum.clear(0); }
    };
    map<int, KeyData> keys;

    PitchEstimation() {
		assert_(audio.channels && audio.duration);
        for(;;) {
            // Input
			buffer<float> period (periodSize*audio.channels);
			if(audio.read(period) < periodSize) break;
			for(size_t i: range(N-periodSize)) signal[i]=signal[i+periodSize];  //FIXME
			for(size_t i: range(periodSize)) signal[N-periodSize+i] = period[i*audio.channels+0] * 0x1p-24; // Left channel only

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
				log(strKey(key)+"\t"_+str(int(round(f*rate/N)),4)+" Hz\t"_+str(int(round(100*offsetF1)),2) +" c\t"_);
                KeyData& data = keys[key];
				data.pitch.append( KeyData::Pitch{estimator.F0, estimator.B} );
                assert(estimator.spectrum.size == N/2);
                for(uint i: range(N/2)) data.spectrum[i] += estimator.spectrum[i];
            }
        }
        // Writes analysis data
        Folder stretch("/var/tmp/stretch"_,root(),true);
        for(int key: keys.keys) {
            const KeyData& data = keys.at(key);
            writeFile(strKey(key)+".f0B"_, cast<byte>(data.pitch), stretch);
            writeFile(strKey(key)+".PSD"_, cast<byte>(data.spectrum), stretch);
        }
    }
} app;
