#pragma once
/// \file midi.h Standard MIDI file player
#include "function.h"
#include "data.h"
#include "map.h"

struct MidiNote { int key; uint start,duration; bool operator <(const MidiNote& o)const{return key<o.key;} };
inline string str(const MidiNote& m) { return str(m.key); }
typedef array<MidiNote> Chord;

enum { NoteOff=8, NoteOn, Aftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend, Meta };
struct Track {
    uint64 time=0; uint8 type_channel=0; BinaryData data;
    Track(uint time, BinaryData&& data):time(time),data(move(data)){}
    uint startTime=0, startIndex=0;
    void reset() { type_channel=0; time=startTime; data.index=startIndex; }
};

/// Standard MIDI file player
struct MidiFile {
    array<Track> tracks;
    int trackCount=0;
    uint16 ticksPerBeat=0;
    uint timeSignature[2] = {4,4}, tempo=60000/120; int key=0; enum {Major,Minor} scale=Major;
    map<int,int> active;
    map<uint,Chord> notes;
    signal<int, int> noteEvent;
    void open(const ref<byte>& data);

    enum State { Seek=0, Play=1, Sort=2 };
    void read(Track& track, uint64 time, State state);
    uint64 time=0;
    void seek(uint64 time);
    void update(uint delta);
    void clear() { tracks.clear(); trackCount=0; ticksPerBeat=0; notes.clear(); active.clear(); }
};
