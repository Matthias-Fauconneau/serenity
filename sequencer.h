#pragma once
#include "process.h"
#include "function.h"

typedef struct _snd_seq snd_seq_t;
struct Sequencer : Poll {
    static const int latency = 1024;
    snd_seq_t* seq;
    array<uint8> pressed;
    array<uint8> sustained;
    bool sustain=false;
    signal<int,int> noteEvent;
    int maxVelocity=96;
    string record;
    struct Event { int16 time; uint8 key; uint8 vel; Event(int16 time, uint8 key, uint8 vel):time(time),key(key),vel(vel){}};
    array<Event> events;
    int lastTick=0;

    Sequencer();
    pollfd poll();
    void setRecord(bool record);
    void event(pollfd);
    void recordMID(const string& path);
    ~Sequencer();
};
