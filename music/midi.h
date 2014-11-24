#pragma once
/// \file midi.h Standard MIDI file player
#include "function.h"
#include "data.h"
#include "map.h"
#include "notation.h"

struct MidiNote { int64 time; uint key, velocity; };
notrace inline bool operator <(const MidiNote& a, const MidiNote& b) { return a.time < b.time || (a.time == b.time && a.key < b.key); }
inline String strKey(int key) { return (string[]){"A","A#","B","C","C#","D","D#","E","F","F#","G","G#"}[(key+2*12+3)%12]+str(key/12-2); }
inline String str(const MidiNote& o) { return str(o.time, strKey(o.key)); }

struct MidiNotes : array<MidiNote> {
	MidiNotes() {}
	MidiNotes(array<MidiNote>&& notes, uint ticksPerSeconds) : array<MidiNote>(::move(notes)), ticksPerSeconds(ticksPerSeconds) {}
	uint ticksPerSeconds=0;
};
inline MidiNotes copy(const MidiNotes& o) { return {copy((array<MidiNote>&)o), o.ticksPerSeconds}; }
inline String str(const MidiNotes& o) { return str(o.ticksPerSeconds, str(ref<MidiNote>(o))); }

enum { NoteOff=8, NoteOn, Aftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend, Meta };
struct Track {
    uint time=0; uint8 type_channel=0; BinaryData data;
    Track(uint time, BinaryData&& data):time(time),data(move(data)){}
    uint startTime=0, startIndex=0;
    void reset() { type_channel=0; time=startTime; data.index=startIndex; }
};

/// Standard MIDI file player
struct MidiFile {
	MidiNotes notes;
    array<Track> tracks;
	uint duration = 0; // Duration in ticks

	//uint timeSignature[2] = {4,4}, tempo=60000/120; int key=0; enum {Major,Minor} scale=Major;
	array<Sign> signs;
	uint divisions = 0; // Time unit (ticks) per beat (quarter note)

	MidiFile() {}
	MidiFile(ref<byte> file);
	virtual void read(Track& track, uint index);
	explicit operator bool() const {  return tracks.size; }
};
