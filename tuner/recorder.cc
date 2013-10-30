#include "thread.h"
#include "math.h"
#include "sampler.h"
#include "time.h"
#include "pitch.h"
#include "sequencer.h"

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
String strKey(int key) { return (string[]){"A"_,"A#"_,"B"_,"C"_,"C#"_,"D"_,"D#"_,"E"_,"F"_,"F#"_,"G"_,"G#"_}[(key+3)%12]+str(key/12-2); }

/// Estimates fundamental frequency (~pitch) of test samples
struct PitchEstimation {
    Thread thread{-20};
    Sequencer input{thread};
    Sampler sampler;
    const uint rate = 48000;
    static constexpr uint N = 8192; // Analysis window size (A-1 (27Hz~2K))
    PitchEstimator pitchEstimator {N};
    Timer timer;
    PitchEstimation() {
        sampler.open(rate, "Salamander.original.sfz"_, Folder("Samples"_,root()));
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        thread.spawn();
        timer.timeout.connect(this, &PitchEstimation::update);
        update();
    }
    void update() {
        int32 output[N*2];
        read(output, N);
        timer.setRelative(N*1000/rate);
    }
    uint read(int32* output, uint size) {
        assert(size%sampler.periodSize==0);
        for(uint i=0; i<size; i+=sampler.periodSize) sampler.read(output+i*2, sampler.periodSize);

        float signal[N];
        for(uint i: range(N)) signal[i] = (output[i*2+0]+output[i*2+1]) * 0x1p-33f;
        float k = pitchEstimator.estimate(signal);
        if(k > rate/keyToPitch(88+1) && k < rate/keyToPitch(0-1)) {
            int key = round(pitchToKey(rate/k));
            float expectedK = rate/keyToPitch(key);
            const float offset = 12*log2(expectedK/k);
            const float error = 12*log2((max(expectedK,k)+1)/max(expectedK,k));
            if(abs(offset) < 1) log(str(rate/k)+" Hz\t"_+strKey(max(0,key))+" \t"_+str(100*offset)+" \t+/-"_+str(100*error)+" cents"_);
        }
        return size;
    }
} test;
