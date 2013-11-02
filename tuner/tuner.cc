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
    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    PitchEstimator pitchEstimator {N};
    AudioInput input{{this,&Tuner::write16}, {this,&Tuner::write32}, 48000, 16384}; // Maximum rate and latency
    const uint rate = input.rate;
    float noiseThreshold = exp2(-8); // Power relative to full scale
    uint fMin = N*440*exp2(-4 - 0./12 - (1./2 / 12))/rate; // ~27 Hz ~ half semitone under A-1
    uint fMax = N*440*exp2(3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half semitone over C7
    float previousPowers[2] = {0,0};
    buffer<float> signal {N}; // Ring buffer
    uint index = 0, unprocessedSize=0;
    Tuner() {
        log(rate,"Hz");
        if(arguments().size>0) { noiseThreshold=exp2(toDecimal(arguments()[0])); log("Noise threshold: ",log2(noiseThreshold),"dB2FS"); }
        if(arguments().size>1) { fMin = toInteger(arguments()[1])*N/rate; log("Minimum frequency"_, fMin, "~"_, fMin*rate/N, "Hz"_); }
        if(arguments().size>2) { fMax = toInteger(arguments()[2])*N/rate; log("Maximum frequency"_, fMax, "~"_, fMax*rate/N, "Hz"_); }
        input.start();
    }
    uint write16(const int16* output, uint size) {
        assert(index+size<=N);
        for(uint i: range(size)) signal[index+i] = (int(output[i*2+0])+int(output[i*2+1])) * 0x1p-17f;  // Replaces oldest period with newest period
        return write(size);
    }
    uint write32(const int32* output, uint size) {
        assert(index+size<=N);
        for(uint i: range(size)) signal[index+i] = (int(output[i*2+0])+int(output[i*2+1])) * 0x1p-33f;  // Replaces oldest period with newest period
        return write(size);
    }
    uint write(uint size) {
        index = (index+size)%N; // Updates ring buffer pointer
        unprocessedSize += size;
        //if(unprocessedSize<N/2) return size; // Limits to 50% overlap
        unprocessedSize = 0;
        buffer<float>& target = pitchEstimator.windowed;
        buffer<float>& window = pitchEstimator.hann;
        for(uint i: range(N-index)) target[i] = window[i]*signal[index+i]; // Direct from ring buffer into window buffer
        for(uint i: range(index)) target[N-index+i] = window[N-index+i]*signal[i]; // idem for tail
        float k = pitchEstimator.estimate({}, fMin, fMax);
        float power = pitchEstimator.power;
        int key = round(pitchToKey(rate/k));

        float expectedK = rate/keyToPitch(key);
        const float kOffset = 12*log2(expectedK/k);
        const float kError = 12*log2((expectedK+1)/expectedK);

        float expectedF = keyToPitch(key)*N/rate;
        float f = pitchEstimator.period ? pitchEstimator.fPeak / pitchEstimator.period : 0;
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


