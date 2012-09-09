#pragma once
#include "process.h"
#include "function.h"

struct Sequencer : Poll {
    static const int latency = 1024;
    uint8 type=0;
    array<uint8> pressed;
    array<uint8> sustained;
    bool sustain=false;
    signal<int,int> noteEvent;
    struct Event { int16 time; uint8 key; uint8 vel; Event(int16 time, uint8 key, uint8 vel):time(time),key(key),vel(vel){}};
    array<Event> events;
    int lastTick=0;
    File record=0;

    Sequencer();
    void setRecord(bool record);
    void event();
    void recordMID(const ref<byte>& path);
    ~Sequencer();
};
