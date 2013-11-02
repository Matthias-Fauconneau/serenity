#include "thread.h"
#include "math.h"
#include "sampler.h"
#include "time.h"
#include "pitch.h"
#include "sequencer.h"
#include "audio.h"

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+3)%12]+str(key/12-2); }

/// Estimates fundamental frequency (~pitch) of audio input
struct Tuner {
    const uint rate = 48000;
    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    PitchEstimator pitchEstimator {N};
    AudioInput input{{this,&Tuner::write}, rate, N}; //CA0110 driver doesn't work
    float noiseThreshold = exp2(-8); // Power relative to full scale
    uint fMin = N*440*exp2(-4 - 0./12 - (1./2 / 12))/rate; // ~27 Hz ~ half semitone under A-1
    uint fMax = N*440*exp2(3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half semitone over C7
    float previousPowers[2] = {0,0};
    Tuner() {
        if(arguments().size>0) { noiseThreshold=exp2(toDecimal(arguments()[0])); log("Noise threshold: ",log2(noiseThreshold),"dB2FS"); }
        if(arguments().size>1) { fMin = toInteger(arguments()[1])*N/rate; log("Minimum frequency"_, fMin, "~"_, fMin*rate/N, "Hz"_); }
        if(arguments().size>2) { fMax = toInteger(arguments()[2])*N/rate; log("Maximum frequency"_, fMax, "~"_, fMax*rate/N, "Hz"_); }
        input.start();
    }
    uint write(int16* output, uint size) {
        float signal[N]; for(uint i: range(N)) signal[i] = (int(output[i*2+0])+int(output[i*2+1])) * 0x1p-17f;
        float k = pitchEstimator.estimate(signal, fMin, fMax);
        float power = pitchEstimator.power;
        int key = round(pitchToKey(rate/k));

        float expectedK = rate/keyToPitch(key);
        const float kOffset = 12*log2(expectedK/k);
        const float kError = 12*log2((expectedK+1)/expectedK);

        float expectedF = keyToPitch(key)*N/rate;
        float f = pitchEstimator.fPeak / pitchEstimator.period;
        const float fOffset =  12*log2(f/expectedF);
        const float fError =  12*log2((expectedF+1)/expectedF);

        if(power > noiseThreshold && previousPowers[1] > power && previousPowers[0] > previousPowers[1]/2) {
            log(strKey(max(0,key+21))+"\t"_ +str(round(rate/k))+" Hz\t"_+dec(log2(power)));
            log("k\t"_+dec(round(100*kOffset))+" \t+/-"_+dec(round(100*kError))+" cents\t"_);
            log("f\t"_+dec(round(100*fOffset))+" \t+/-"_+dec(round(100*fError))+" cents\t"_);
        }

        previousPowers[0] = previousPowers[1];
        previousPowers[1] = power;
        return size;
    }
} app;


