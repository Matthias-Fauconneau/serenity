#pragma once
#include "function.h"
#include "file.h"
#include "resample.h"
#include "flac.h"
#include "process.h"

struct Sample {
    Map map; //Sample Definition
    int16 trigger=0; int16 lovel=0; int16 hivel=127; int16 lokey=0; int16 hikey=127; //Input Controls
    int16 pitch_keycenter=60; int32 releaseTime=0; int16 amp_veltrack=100; int16 rt_decay=0; int16 volume=1; //Performance Parameters
};

struct Note : FLAC /*3.8*/ {
    Note(const ref<byte>& buffer):FLAC(buffer){}
    uint8 layer; //Resampling layer where the note shold be mixed in
    uint8 key=0; //used to match release samples
    uint position=0; //current stream position in samples
    float2 level; // current note attenuation [16-aligned]
    uint releaseStart; // position in stream to start fade out
    float2 step; //coefficient for release fade out = (2 ** -24)**(1/releaseTime) [16-aligned]
    /// Mix in \a size samples into \a out and returns whether the note is still active
    template<bool mix> bool read(float2* out, uint size);
    /// Computes root mean square on the next \a count samples
    float rootMeanSquare(uint size);
};

struct Sampler : Poll {
    //FIXME: resampling is rounded to feed same input size for each period (causing a tuning relative error of 1/period)
    //FIXME: on the other hand, samples can only begin at the start of each period
    static constexpr uint period = 512; // every 11ms (94Hz), 3 cents error

    array<Sample> samples;
    array<Note> active;
    struct Event { int key,velocity; }; array<Event> queue; //starting all release samples at once when releasing pedal might trigger an overrun
    struct Layer { float2* buffer=0; uint size=0; bool active=false; Resampler resampler; } layers[3];
    File record=0;
    int16* pcm = 0; int time = 0;
    signal<int> timeChanged;
    operator bool() const { return samples.size(); }

    void open(const ref<byte>& path);

    void lock();
    uint full=0,available=0,current=0;
    void event();
    signal<int, int> progressChanged;

    void queueEvent(int key, int velocity);
    void processEvent(Event e);
    bool read(int16* output, uint size);
    void recordWAV(const ref<byte>& path);
    ~Sampler();
};
