#pragma once
/// \file midi.h Standard MIDI file player
#include "function.h"
#include "data.h"
#include "map.h"

enum { NoteOff=8, NoteOn, Aftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend, Meta };
struct Track { uint time=0; int type=0; BinaryData data; Track(uint time, BinaryData&& data):time(time),data(move(data)){} };

/// Standard MIDI file player
struct MidiFile {
    array<Track> tracks;
    int trackCount=0;
    uint midiClock=0;
    typedef array<int> Chord;
    map<int,Chord> notes;
    signal<int, int> noteEvent;
    void open(const ref<byte>& path);

    enum State { Seek=0, Play=1, Sort=2 };
    void read(Track& track, uint time, State state);
    uint time=0;
    void seek(uint time);
    void update(uint delta);

};
