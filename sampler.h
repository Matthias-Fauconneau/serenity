#pragma once
#include "signal.h"
#include "string.h"
#include "resample.h"
#include "flac.h"

struct Sample {
    const byte* data=0; int size=0; //Sample Definition
    int16 trigger=0; int16 lovel=0; int16 hivel=127; int16 lokey=0; int16 hikey=127; //Input Controls
    int16 pitch_keycenter=60; int32 release=48000; int16 amp_veltrack=100; int16 rt_decay=0; float volume=1; //Performance Parameters
};

struct Note : FLAC { Note(array<byte>&& buffer):FLAC(move(buffer)){} int remaining; int release; int key; int layer; int velocity; float level; };

struct Sampler {
    static const uint period = 1024; //-> latency

    array<Sample> samples;
    array<Note> active;
    struct Layer { float* buffer; uint size; bool active=false; Resampler resampler; } layers[3];
    int record=0;
    int16* pcm = 0; int time = 0;
    signal<int> timeChanged;
    operator bool() const { return samples.size(); }

    void open(const string& path);
    void event(int key, int vel);
    void read(int16* output, uint size);
    void recordWAV(const string& path);
    ~Sampler();
};
