#include "thread.h"
#include "math.h"
#include "sampler.h"
#include "time.h"
#include "pitch.h"
#include "sequencer.h"
#include "audio.h"

/// Estimates fundamental frequency (~pitch) of audio input
struct Tuner {
    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    PitchEstimator pitchEstimator {N};
    AudioInput input{{this,&Tuner::write16}, {this,&Tuner::write32}, 48000, N};

    const uint rate = input.rate;
    float noiseThreshold = exp2(-8); // Power relative to full scale
    uint fMin = N*440*exp2(-4 - 0./12 - (1./2 / 12))/rate; // ~27 Hz ~ half semitone under A-1
    uint fMax = N*440*exp2(3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half semitone over C7
    float previousPowers[2] = {0,0};
    buffer<float> signal {N}; // Ring buffer
    uint index = 0, unprocessedSize=0;
    Notch notch0 {50./48000, 1}; // Notch filter to remove 50Hz noise
    Notch notch1 {150./48000, 1}; // Cascaded to remove the first odd harmonic (3rd partial)
    Tuner() {
        log(input.sampleBits, input.rate, input.periodSize);
        if(arguments().size>0) { noiseThreshold=exp2(toDecimal(arguments()[0])); log("Noise threshold: ",log2(noiseThreshold),"dB2FS"); }
        if(arguments().size>1) { fMin = toInteger(arguments()[1])*N/rate; log("Minimum frequency"_, fMin, "~"_, fMin*rate/N, "Hz"_); }
        if(arguments().size>2) { fMax = toInteger(arguments()[2])*N/rate; log("Maximum frequency"_, fMax, "~"_, fMax*rate/N, "Hz"_); }
        input.start();
    }
    uint write16(const int16* output, uint size) {
        assert(index+size<=N);
        for(uint i: range(size)) signal[index+i] = notch0(notch1( (int(output[i*2+0])+int(output[i*2+1])) * 0x1p-17f ));
        return write(size);
    }
    uint write32(const int32* output, uint size) {
        assert(index+size<=N);
        for(uint i: range(size)) signal[index+i] = notch0(notch1( (int(output[i*2+0])+int(output[i*2+1])) * 0x1p-33f ));
        return write(size);
    }
    uint write(uint size) {
        index = (index+size)%N; // Updates ring buffer pointer
        unprocessedSize += size;
        //if(unprocessedSize<N/2) return size; // Limits to 50% overlap
        unprocessedSize = 0;
        buffer<float> buffer {N}; // Need a linear buffer for autocorrelation (FIXME: mmap ring buffer)
        for(uint i: range(N-index)) buffer[i] = signal[index+i]; // Copies head into ring buffer
        for(uint i: range(index)) buffer[N-index+i] = signal[i]; // Copies tail into ring buffer
        float k = pitchEstimator.estimate(buffer, fMin, fMax);
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
            log(strKey(max(0,key))+"\t"_ +str(round(rate/k))+" Hz\t"_+dec(log2(power)));
            log("k\t"_+dec(round(100*kOffset))+" \t+/-"_+dec(round(100*kError))+" cents\t"_);
            log("f\t"_+dec(round(100*fOffset))+" \t+/-"_+dec(round(100*fError))+" cents\t"_);
        }

        previousPowers[0] = previousPowers[1];
        previousPowers[1] = power;
        return size;
    }
} app;


