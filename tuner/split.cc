#include "thread.h"
#include "math.h"
#include "pitch.h"
#include <fftw3.h> //fftw3f
#include "flac.h"
#include "plot.h"
#include "window.h"

inline float keyToPitch(float key, float A3=440) { return A3*exp2((key-69)/12); }
inline float pitchToKey(float pitch, float A3=440) { return 69+log2(pitch/A3)*12; }
String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+3)%12]+str(key/12-2); }

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    Plot plot;
    Window window {&plot, int2(1024, 768), "Tuner"};
    const uint rate = 48000;
    static constexpr uint N = 8192; // Analysis window size (A-1 (27Hz~2K))

    PitchEstimator pitchEstimator {N};
    int lastKey = 0;
    float previousEnergies[2] = {0,0};

    PitchEstimation() {
        buffer<float2> stereo = decodeAudio(Map("Samples/all.flac"_));
        map<float,float> offsets;
        for(uint t=0; t<=stereo.size-N; t+=N) {
            const float2* period = stereo + t;
            float signal[N]; float e=0;
            for(uint i: range(N)) {
                signal[i] = (period[i][0]+period[i][1]) * 0x1p-24f;
                e += sq(signal[i]);
            }
            const uint fMin = 1; //ceil(N*50./rate); // Unamplified microphone recording has very strong peak at 50Hz (utility frequency) and odd harmonics
            //const uint fMax = N*440*pow(2,  3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half pitch over C7
            float k = pitchEstimator.estimate(signal, fMin/*, fMax*/);
            int key = 0;
            //if(k > rate/keyToPitch(21+88+1) && k < rate/keyToPitch(21-1)) {
            float A3 = 453; //TODO: optimize A3 for least offset error
            key = round(pitchToKey(rate/k, A3))-21;
            float expectedK = rate/keyToPitch(key+21, A3);
            const float offset = 12*log2(expectedK/k);
            const float error = 12*log2((max(expectedK,k)+1)/max(expectedK,k));
            //if(abs(offset) < 1 && key==lastKey && previousEnergies[0]<previousEnergies[1] && previousEnergies[1]>e) {
            if(e>2) {
                //float velocity = 0x100*sqrt(e/N);
                log(str(round(rate/k))+" Hz\t"_+strKey(max(0,key+21))+" \t"_+str(100*offset)+" \t+/-"_+str(100*error)+" cents\t"_+str(e));
                //}
                offsets.insertMulti(key, 100*12*log2(expectedK/k));
            }
            //}
            lastKey = key;
            previousEnergies[0] = previousEnergies[1];
            previousEnergies[1] = e;
        }

        plot.title = String("Pitch ratio (in cents) to equal temperament (A440)"_);
        plot.xlabel = String("Key"_), plot.ylabel = String("Cents"_);
        plot.dataSets << move(offsets);

        window.backgroundColor=window.backgroundCenter=1;
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
    }
} app;
