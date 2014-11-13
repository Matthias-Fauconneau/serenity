#pragma once
/// notation.h Music notation definitions
#include "core.h"

enum ClefSign { Bass, Treble };
enum Accidental { None, DoubleFlat /*♭♭*/, Flat /*♭*/, Natural /*♮*/, Sharp /*♯*/, DoubleSharp /*♯♯*/ };
enum Duration { Whole, Half, Quarter, Eighth, Sixteenth, Thirtysecond, Sixtyfourth };
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
    Duration duration;
    enum Tie { NoTie, TieStart, TieContinue, TieStop } tie;
    bool dot:1;
    bool grace:1;
    bool slash:1;
    bool staccato:1;
    bool tenuto:1;
    bool accent:1;
    bool stem:1; // 0: down, 1: up
    uint key; // MIDI key
	size_t measureIndex, glyphIndex;
};
struct Rest {
    Duration duration;
};
struct Measure { uint measure, page, pageLine, lineMeasure; };
/*struct Pedal {
    PedalAction action;
};*/
/*struct Wedge {
    WedgeAction action;
};*/
/*struct Dynamic {
	//Loudness loudness;
};*/
struct KeySignature {
    int fifths; // Index on the fifths circle
};
struct TimeSignature {
    uint beats, beatUnit;
};
struct Metronome {
    Duration beatUnit;
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
