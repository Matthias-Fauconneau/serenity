#pragma once
/// notation.h Music notation definitions
#include "string.h"

inline bool isPowerOfTwo(uint v) { return !(v & (v - 1)); }

enum ClefSign { Bass, Treble, NoClef };
enum Accidental { None, Flat /*♭*/, Natural /*♮*/, Sharp /*♯*/, DoubleFlat /*♭♭*/, DoubleSharp /*♯♯*/ };
static constexpr string accidentalNames[] = {""_,"flat"_,"natural"_,"sharp"_,"double-flat"_,"double-sharp"_};
static constexpr string accidentalNamesLy[] = {""_,"flat"_,"natural"_,"sharp"_,"flatflat"_,"doublesharp"};
enum Value { InvalidValue=-1,                  Double, Whole, Half, Quarter, Eighth, Sixteenth, Thirtysecond, Sixtyfourth };
static constexpr string valueNames[] = {"double"_,"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_,"32nd"_,"64th"_};
static constexpr uint valueDurations[] = {128,       64,         32,       16,           8,             4,         2,         1};
static constexpr uint quarterDuration = 16;
enum Pedal { Ped=-1, Start, Change, PedalStop };
enum Wedge{ Crescendo, Diminuendo, WedgeStop };
enum OctaveShift { Down, Up, OctaveStop };
enum class Repeat { Begin=-2, End=-1, None=0 };
enum Break { NoBreak, LineBreak, PageBreak };

struct Clef {
    ClefSign clefSign;
    int octave;
};
struct Note {
    Clef clef;
    int step; // 0 = C4
    Accidental accidental;
	Value value;
	enum Tie { NoTie, TieStart, TieContinue, TieStop, Merged } tie;
    bool dot:1;
    bool grace:1;
    bool slash:1;
    bool staccato:1;
    bool tenuto:1;
    bool accent:1;
	bool trill:1;
    bool stem:1; // 0: down, 1: up
	uint tuplet;
    uint key; // MIDI key
	size_t measureIndex = invalid, glyphIndex = invalid;
	uint duration() const {
		uint duration = valueDurations[value];
		if(dot) duration = duration * 3 / 2;
		if(tuplet) duration = duration * (tuplet-1) / tuplet;
		return duration;
	};
};
struct Rest {
	Value value;
	uint duration() const { return valueDurations[value]; };
};
struct Measure {
	Break lineBreak;
	uint measure, page, pageLine, lineMeasure;
	//enum Repeat repeat;
};
struct KeySignature {
    int fifths; // Index on the fifths circle
};
struct TimeSignature {
	uint beats, beatUnit;
};
struct Metronome {
	Value beatUnit;
    uint perMinute;
};

struct Sign {
	int64 time; // Absolute time offset
	int duration;
	uint staff; // Staff index (-1: all)
	enum {
		Invalid,
		Note, Rest, Clef, // Staff
		Metronome, OctaveShift, // Top
		Dynamic, Wedge, // Middle
		Pedal, // Bottom
		Measure, KeySignature, TimeSignature, Repeat // Across
	} type;
	union {
		struct Note note;
		struct Rest rest;
		struct Measure measure;
		struct Clef clef;
		struct KeySignature keySignature;
		struct TimeSignature timeSignature;
		struct Metronome metronome;
		string dynamic;
		enum Pedal pedal;
		enum Wedge wedge;
		enum OctaveShift octave;
		enum Repeat repeat;
	};
};
inline bool operator <(const Sign& a, const Sign& b) {
    if(a.time==b.time) {
		if(a.type==Sign::Note && b.type==Sign::Note) return a.note.step < b.note.step;
    }
	return a.time < b.time;
}
inline String strKey(int key) { return (string[]){"A","A#","B","C","C#","D","D#","E","F","F#","G","G#"}[(key+2*12+3)%12]+str(key/12-2); }
inline String str(const Sign& o) {
	if(o.type==Sign::Note) return strKey(o.note.key);
	error(int(o.type));
}
