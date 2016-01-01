#pragma once
/// \file sequencer.h ALSA MIDI input interface
#include "thread.h"
#include "function.h"
#include "vector.h"

/// MIDI input through ALSA rawmidi interface
struct MidiInput : Device, Poll {
    buffer<int> min;
    buffer<int> max;
    uint8 type=0;
    array<uint8> pressed;
    array<uint8> sustained;
    bool sustain=false;
    function<void(uint,uint)> ccEvent;
    function<void(uint,uint/*,float2*/)> noteEvent;
    struct Event { int16 time; uint8 key; uint8 vel; Event(int16 time, uint8 key, uint8 vel):time(time),key(key),vel(vel){}};
    array<Event> events;
    int lastTick=0;

    MidiInput(Thread& thread=mainThread);
    ~MidiInput();
    void event();
};
