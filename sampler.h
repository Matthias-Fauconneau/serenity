#pragma once
#include "function.h"
#include "file.h"
#include "resample.h"
#include "flac.h"

struct Sample {
    Map data; //Sample Definition
    int16 trigger=0; int16 lovel=0; int16 hivel=127; int16 lokey=0; int16 hikey=127; //Input Controls
    int16 pitch_keycenter=60; int32 release=48000; int16 amp_veltrack=100; int16 rt_decay=0; int16 volume=1; //Performance Parameters
};

struct Note : FLAC {
    float* block; //current position in FLAC::buffer (last decoded FLAC block)
    int remaining; // remaining frames before release or decay
    int release; int key; int layer; int velocity; float level; // \sa Sample
    template<bool mix> void Note::read(float* out, int size);
};

struct Sampler {
    //FIXME: resampling is rounded to feed same input size for each period (causing a tuning relative error of 1/period)
    //FIXME: on the other hand, samples can only begin at the start of each period
    static constexpr uint period = 512; // every 11ms (94Hz), 3 cents error

    array<Sample> samples; //88*16
    array<Note> active;
    struct Event { int key,velocity; }; array<Event> queue; //starting all release samples at once when releasing pedal might trigger an overrun
    struct Layer { float* buffer=0; uint size=0; bool active=false; Resampler resampler; } layers[3];
    File record __(0);
    int16* pcm = 0; int time = 0;
    signal<int> timeChanged;
    operator bool() const { return samples.size(); }

    void open(const ref<byte>& path);
    void lock();
    void queueEvent(int key, int velocity);
    void processEvent(Event e);
    void read(int16* output, uint size);
    void recordWAV(const string& path);
    ~Sampler();
};
