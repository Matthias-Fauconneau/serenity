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
    static constexpr uint N = 8192; // Analysis window size (A-1 (27Hz~2K))

    PitchEstimator pitchEstimator {N};
    Timer timer;
#if RECORD
    int lastKey = 0;
    buffer<int16> previousFrames[2];
    float previousEnergies[2] = {0,0};
#endif
#define CAPTURE 1
#if CAPTURE
    AudioInput input{{this,&Tuner::write}, rate, N}; //CA0110 driver doesn't work
    Tuner() { input.start(); }
#else
    Thread thread{-20};
    Sequencer input{thread};
    Sampler sampler;
    Recorder() {
        sampler.open(rate, "Salamander.original.sfz"_, Folder("Samples"_,root()));
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        thread.spawn();
        timer.timeout.connect(this, &Recorder::update);
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
        return write(output, size);
    }
    uint write(int32* output, uint size) {
        int16 buffer[N*2];
        for(uint i: range(N*2)) buffer[i] = output[i]>>16;
        return write(buffer, size);
    }
#endif
    uint write(int16* output, uint size) {
        buffer<int16> frame {N*2};
        float signal[N]; float e=0;
        for(uint i: range(N)) {
            frame[i*2+0] = output[i*2+0];
            frame[i*2+1] = output[i*2+1];
            signal[i] = (int(output[i*2+0])+int(output[i*2+1])) * 0x1p-17f;
            e += sq(signal[i]);
        }
        float k = pitchEstimator.estimate(signal);
        int key = 0;
        if(k > rate/keyToPitch(21+88+1) && k < rate/keyToPitch(21-1)) {
            key = round(pitchToKey(rate/k))-21;
            float expectedK = rate/keyToPitch(key+21);
            const float offset = 12*log2(expectedK/k);
            const float error = 12*log2((max(expectedK,k)+1)/max(expectedK,k));
            if(e > N/64) { // -6 dB2FS
#if RECORD
                if(abs(offset) < 1 && key==lastKey && previousEnergies[0]<previousEnergies[1] && previousEnergies[1]>e) {
                    Folder samples("samples"_, home(), true);
                    int velocity = round(0x100*sqrt(previousEnergies[1]/N));
                    int maxVelocity = 0;
                    buffer<int> keys (88); clear(keys.begin(), keys.size);
                    for(string name: samples.list(Files)) {
                        TextData s (name); int fKey = s.integer(true); if(!s.match('-')) continue; int fVelocity = s.hexadecimal();
                        if(key==fKey && fVelocity > maxVelocity) maxVelocity = fVelocity;
                        if(fKey>=0 && fKey<88 && fVelocity>keys[(uint)fKey]) keys[(uint)fKey] = min(0xFF,fVelocity);
                    }
                    if(velocity >= maxVelocity) { // Records new file only if higher velocity than any existing
                        writeFile(dec(key,2)+"-"_+hex(velocity,2), cast<byte>(previousFrames[0]+previousFrames[1]+frame), samples);
                        if(key>=0 && key<88) keys[(uint)key] = min(0xFF,velocity);
                    }
                    log(apply(keys,[](int v){ return "0123456789ABCDF"_[(uint)v/0x10]; }));
                }
#endif
                int velocity = round(0x100*sqrt(e/N));
                log(str(rate/k)+" Hz\t"_+strKey(max(0,key+21))+" \t"_+str(100*offset)+" \t+/-"_+str(100*error)+" cents\t"_+str(velocity)+" "_+str(e));
            }
        }
#if RECORD
        lastKey = key;
        previousEnergies[0] = previousEnergies[1];
        previousEnergies[1] = e;
        previousFrames[0] = move(previousFrames[1]);
        previousFrames[1] = move(frame);
#endif
        return size;
    }
} app;
