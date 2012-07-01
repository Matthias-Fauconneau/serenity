#pragma once
//#include "map.h"
#include "signal.h"
#include "stream.h"
#include "string.h"

struct Track { int time=0; int type=0; DataStream stream; Track(DataStream&& stream):stream(move(stream)){} };

struct MidiFile {
    enum { NoteOff=8, NoteOn, Aftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend, Meta };
    enum { SequenceNumber, Text, Copyright, TrackName, InstrumentName, Lyrics, Marker, Cue, ChannelPrefix=0x20,
           EndOfTrack=0x2F, Tempo=0x51, Offset=0x54, TimeSignature=0x58, KeySignature, SequencerSpecific=0x7F };
    enum State { Seek=0, Play=1, Sort=2 };
    array<Track> tracks;
    int trackCount=0;
    int midiClock=0;
    //map<int, map<int, int> > sort; //[chronologic][bass to treble order] = index
    signal<int, int> noteEvent;

    void open(const string& path);
    void read(Track& track, int time, State state);
    void seek(int time);
    void update(int time);
};
