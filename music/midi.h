#pragma once
/// \file midi.h Standard MIDI file player
#include "function.h"
#include "data.h"
#include "map.h"
#include "notation.h"

struct MidiNote { uint time, key, velocity; };
notrace inline bool operator ==(const MidiNote& a, const MidiNote& b) { return a.time == b.time && a.key == b.key && a.velocity == b.velocity; }
notrace inline bool operator <(const MidiNote& a, const MidiNote& b) { return a.time < b.time || (a.time == b.time && a.key < b.key); }
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

	array<Sign> signs;
	uint divisions = 0; // Time unit (ticks) per beat (quarter note)

	MidiFile() {}
	MidiFile(ref<byte> file);
	explicit operator bool() const {  return tracks.size; }
};
