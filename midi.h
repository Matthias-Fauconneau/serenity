#pragma once
/// \file midi.h Standard MIDI file player
#include "function.h"
#include "data.h"
#include "map.h"

enum { NoteOff=8, NoteOn, Aftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend, Meta };
struct Track {
    uint time=0; uint8 type=0; BinaryData data;
    Track(uint time, BinaryData&& data):time(time),data(move(data)){}
    uint startTime=0, startIndex=0;
    void reset() { type=0; time=startTime; data.index=startIndex; }
};

/// Standard MIDI file player
struct MidiFile {
    array<Track> tracks;
    int trackCount=0;
    uint midiClock=0;
    uint minDelta=-1;
    typedef array<int> Chord;
    map<int,Chord> notes;
    signal<int, int> noteEvent;
    void open(const ref<byte>& path);

    enum State { Seek=0, Play=1, Sort=2 };
    void read(Track& track, uint time, State state);
    uint time=0;
    void seek(uint time);
    void update(uint delta);
    void clear() { tracks.clear(); trackCount=0; midiClock=0; notes.clear(); }
};
