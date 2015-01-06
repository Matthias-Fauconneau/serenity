#pragma once
/// \file sequencer.h ALSA MIDI input interface
#include "thread.h"
#include "function.h"

/// MIDI input through ALSA rawmidi interface
struct MidiInput : Device, Poll {
    uint8 type=0;
    array<uint8> pressed;
    array<uint8> sustained;
    bool sustain=false;
    signal<uint,uint> noteEvent;
    struct Event { int16 time; uint8 key; uint8 vel; Event(int16 time, uint8 key, uint8 vel):time(time),key(key),vel(vel){}};
    array<Event> events;
    int lastTick=0;

    MidiInput(Thread& thread=mainThread);
    void event();
};
