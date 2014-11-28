#pragma once
/// notation.h Music notation definitions
#include "string.h"

enum ClefSign { Bass, Treble };
enum Accidental { None, Flat /*♭*/, Natural /*♮*/, Sharp /*♯*/, DoubleFlat /*♭♭*/, DoubleSharp /*♯♯*/ };
enum Value { Double, Whole, Half, Quarter, Eighth, Sixteenth, Thirtysecond, Sixtyfourth };
static constexpr uint quarterDuration = 16;
static constexpr uint valueDurations[] = {128,64,32,16,8,4,2,1};
enum Pedal { Ped=-1, Start, Change, PedalStop };
enum Wedge{ Crescendo, Diminuendo, WedgeStop };
enum OctaveShift { Down, Up, OctaveStop };
enum SlurType { SlurStart, SlurStop };

struct Clef {
    ClefSign clefSign;
    int octave;
};
struct Note {
    Clef clef;
    int step; // 0 = C4
    Accidental accidental;
	Value value;
    enum Tie { NoTie, TieStart, TieContinue, TieStop } tie;
    bool dot:1;
    bool grace:1;
    bool slash:1;
    bool staccato:1;
    bool tenuto:1;
    bool accent:1;
	bool trill:1;
    bool stem:1; // 0: down, 1: up
    uint key; // MIDI key
	size_t measureIndex = invalid, glyphIndex = invalid;
	uint duration() const {
		uint duration = valueDurations[value];
		if(dot) duration = duration * 3 / 2;
		//TODO: tuplet
		return duration;
	};
};
struct Rest {
	Value value;
};
struct Measure { uint measure, page, pageLine, lineMeasure; };
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
struct Slur {
    size_t documentIndex;
    int index;
    SlurType type;
    bool matched;
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
		Measure, KeySignature, TimeSignature, // Across
		Slur // Toggle (Staff/Across)
	} type;
	union {
		struct Note note;
		struct Rest rest;
		struct Measure measure;
		struct Clef clef;
		struct KeySignature keySignature;
		struct TimeSignature timeSignature;
		struct Metronome metronome;
		//struct Dynamic dynamic;
		string dynamic;
		enum Pedal pedal;
		enum Wedge wedge;
		enum OctaveShift octave;
		struct Slur slur;
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
