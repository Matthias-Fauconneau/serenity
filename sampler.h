#pragma once
#include "function.h"
#include "string.h"
#include "file.h"
#include "resample.h"
#include "flac.h"

struct Sample {
    Map data; //Sample Definition
    int16 trigger=0; int16 lovel=0; int16 hivel=127; int16 lokey=0; int16 hikey=127; //Input Controls
    int16 pitch_keycenter=60; int32 release=48000; int16 amp_veltrack=100; int16 rt_decay=0; float volume=1; //Performance Parameters
};

struct Note : FLAC { int remaining; int release; int key; int layer; int velocity; float level; };

struct Sampler {
    static const uint period = 1024; //-> latency

    static_array<Sample,88*16> samples;
    static_array<Note,128> active; //8MB
    struct Layer { float* buffer; uint size; bool active=false; Resampler resampler; } layers[3];
    File record=0;
    int16* pcm = 0; int time = 0;
    signal<int> timeChanged;
    operator bool() const { return samples.size(); }

    void open(const string& path);
    void lock();
    void event(int key, int vel);
    void read(int16* output, uint size);
    void recordWAV(const string& path);
    ~Sampler();
};
