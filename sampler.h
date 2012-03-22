#pragma once
#include "signal.h"
#include "string.h"
#include "file.h"
#include "resample.h"
#include "flac.h"

/// \a static_array is an \a array with preallocated inline space
/// \note as static_array use static inheritance (to avoid code duplication), it cannot extend \a array small inline space.
///          \a array inline space will be wasted and \a array methods will use the \a array::buffer reference
template<class T, int N> struct static_array : array<T> {
    static_array(static_array&&)=delete; static_array operator=(static_array&&)=delete; //TODO
    static_array() { array<T>::tag=-2; array<T>::buffer=typename array<T>::Buffer((T*)buffer,0,N); }
    ubyte buffer[N*sizeof(T)];
};

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
    int record=0;
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
