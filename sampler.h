#pragma once
#include "function.h"
#include "file.h"
#include "resample.h"
#include "flac.h"
#include "process.h"

struct Sample {
    Map data; //Sample Definition
    int16 trigger=0; int16 lovel=0; int16 hivel=127; int16 lokey=0; int16 hikey=127; //Input Controls
    int16 pitch_keycenter=60; int32 release=48000; int16 amp_veltrack=100; int16 rt_decay=0; int16 volume=1; //Performance Parameters
};

struct Note : FLAC {
    float* block; //current position in FLAC::buffer (last decoded FLAC block)
    uint remaining; // remaining frames before release or decay
    uint release; int key; int layer; int velocity; float level; // \sa Sample
    Note(const ref<byte>& buffer):FLAC(buffer){}
    template<bool mix> void read(float* out, uint size);
};

struct Sampler : Poll {
    //FIXME: resampling is rounded to feed same input size for each period (causing a tuning relative error of 1/period)
    //FIXME: on the other hand, samples can only begin at the start of each period
    static constexpr uint period = 512; // every 11ms (94Hz), 3 cents error

    array<Sample> samples;
    array<Note> active;
    struct Event { int key,velocity; }; array<Event> queue; //starting all release samples at once when releasing pedal might trigger an overrun
    struct Layer { float* buffer=0; uint size=0; bool active=false; Resampler resampler; } layers[3];
    File record __(0);
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
