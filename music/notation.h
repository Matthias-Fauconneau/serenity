#pragma once
/// notation.h Music notation definitions
#include "string.h"

inline bool isPowerOfTwo(uint v) { return !(v & (v - 1)); }

struct PitchClass {
	char keyIntervals[11 +1];
	char accidentals[12 +1]; // 0=none, 1=♭, 2=♮, 3=♯
};
static constexpr PitchClass pitchClasses[12] = { //*7%12 //FIXME: generate
	// C♯D♯EF♯G♯A♯B    C♯D♯EF♯G♯A♯B
	{"10101101010", "N.N.N..N.N.N"}, // ♭♭♭♭♭♭ G♭/F# e♭/d#
	{"10101101010", "..N.N..N.N.N"}, // ♭♭♭♭♭/♯♯♯♯♯♯♯ D♭ b♭
	{"10101101010", "..N.N.b..N.N"}, // ♭♭♭♭ A♭ f
	{"10101101010", ".b..N.b..N.N"}, // ♭♭♭ E♭ c
	{"10101101010", ".b..N.b.b..N"}, // ♭♭ B♭ g
	{"10101101010", ".b.b..b.b..N"}, // ♭ F d
	// C♯D♯EF♯G♯A♯B    C♯D♯EF♯G♯A♯B
	{"01011010101", ".#.#..#.#.#."},  // ♮ C a '.b.b..b.b.b.'
	{"01011010101", ".#.#.N..#.#."},  // ♯ G e
	{"01011010101",  "N..#.N..#.#."},  // ♯♯ D b
	{"01011010101", "N..#.N.N..#."},  // ♯♯♯ A f♯
	{"01011010101", "N.N..N.N..#."},  // ♯♯♯♯ E c♯
	{"01011010101", "N.N..N.N.N.."}  // ♯♯♯♯♯/♭♭♭♭♭♭♭ B g♯
};
inline int keyStep(int key, int fifths) {
	assert_(fifths >= -6 && fifths <= 6, fifths);
	int h=key/12*7; for(int i: range(key%12)/*0-10*/) h+=pitchClasses[fifths+6].keyIntervals[i]-'0';
	return h - 35; // 0 = C4;
}

namespace SMuFL { //Standard Music Font Layout
	namespace NoteHead { enum { Double=0xE0A0, Square, Whole, Half, Black }; }
	namespace Rest { enum { Maxima = 0xE4E0, Longa, Double, Whole, Half, Quarter, Eighth, Sixteenth, Thirtysecond, Sixtyfourth }; }
	namespace Clef {  enum { G=0xE050, F=0xE062 }; enum { _15mb=1, _8vb, _8va, _15ma }; }
	namespace TimeSignature {  enum { _0=0xE080 }; }
	enum { Dot=0xE1E7 };
	namespace Flag { enum { Above=0xE240, Below }; }
	enum Accidental { None=0, AccidentalBase=0xE260, Flat=AccidentalBase, Natural, Sharp, DoubleSharp, DoubleFlat };
	static constexpr string accidental[] = {"flat"_,"natural"_,"sharp"_,"double-sharp"_,"double-flat"_};
	namespace Articulation { enum { Base=0xE4A0, Accent=0, Staccato, Tenuto }; }
	enum Dynamic { DynamicBase=0xE520/*Piano=DynamicBase, Mezzo, Forte, Rinforzando, Sforzando, z, n, pppppp, ppppp, pppp, ppp, pp, mp, mf, pf, ff, fff, ffff, fffff, ffffff,
				   fp, fz, sf, sfp, sfpp, sfz, sfzp, sffz, rf, rfz*/ };
	static constexpr string dynamic[] = {
		"p", "m", "f", "r", "s", "z", "n", "pppppp", "ppppp", "pppp", "ppp", "pp", "mp", "mf", "pf", "ff", "fff", "ffff", "fffff", "ffffff",
		"fp", "fz", "sf", "sfp", "sfpp", "sfz", "sfzp", "sffz", "rf", "rfz"};
	namespace Pedal { enum { Mark = 0xE650 }; }
}

enum ClefSign { NoClef=0, FClef=SMuFL::Clef::F, GClef=SMuFL::Clef::G };
using Dynamic = SMuFL::Dynamic;
using Accidental = SMuFL::Accidental;
enum Value { InvalidValue=-1,                  /*Maxima, Longa, Double,*/ Whole, Half, Quarter, Eighth, Sixteenth, Thirtysecond, Sixtyfourth };
static constexpr string valueNames[] = {/*"maxima","longa","double"_,*/"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_,"32nd"_,"64th"_};
static constexpr uint valueDurations[] = {/*512, 256, 128,*/       64,         32,       16,           8,             4,         2,         1};
static constexpr uint quarterDuration = 16;
enum Pedal { Ped=-1, Start, Change, PedalStop };
enum Wedge{ Crescendo, Diminuendo, WedgeStop };
enum OctaveShift { Down, Up, OctaveStop };
enum class Repeat { Begin=-2, End=-1, None=0 };
enum Break { NoBreak, LineBreak, PageBreak };

inline int keyAlteration(int key, int fifths) {
	assert_(fifths >= -6 && fifths < 6, fifths);
	char c = pitchClasses[fifths+6].accidentals[key%12/*0-11*/];
	if(c== 'b') return -1;
	if(c== '.' || c=='N') return 0;
	if(c=='#') return 1;
	error(c);
}
inline Accidental alterationAccidental(int alteration) {
	return ref<Accidental>{Accidental::Flat, Accidental::Natural, Accidental::Sharp}[alteration+1];
}
inline int accidentalAlteration(Accidental accidental) { return ref<int>{-1,0,1}[accidental - Accidental::AccidentalBase]; }

struct Clef {
    ClefSign clefSign;
    int octave;
};
struct Note {
	Clef clef; // Explicit current clef context for convenience
	int step; // Independent from clef (0 = C4)
	int alteration;
	Accidental accidental;
	uint key; // MIDI key
	Value value;
	enum Tie { NoTie, TieStart, TieContinue, TieStop, Merged } tie;
	uint tuplet;
	bool dot = false;
	bool grace = false;
	bool acciaccatura = false; // Before principal beat (slashed)
	bool accent = false;
	bool staccato = false;
	bool tenuto = false;
	bool trill = false;
	int finger = 0;
	//bool stem:1; // 0: undefined, 1: down, 2: up
	size_t pageIndex = invalid, measureIndex = invalid, glyphIndex = invalid;
	int tieStartNoteIndex = 0; // Invalidated by any insertion/deletion
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
};
typedef int KeySignature; // Index on the fifths circle
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
		::Note note;
		::Rest rest;
		::Measure measure;
		::Clef clef;
		::KeySignature keySignature;
		::TimeSignature timeSignature;
		::Metronome metronome;
		::Dynamic dynamic;
		::Pedal pedal;
		::Wedge wedge;
		::OctaveShift octave;
		::Repeat repeat;
	};
};

inline bool operator <(const Sign& a, const Sign& b) {
    if(a.time==b.time) {
		if(a.type==Sign::Note && b.type==Sign::Note) return a.note.step < b.note.step;
    }
	return a.time < b.time;
}

inline String strKey(int key) { return (string[]){"A","A#","B","C","C#","D","D#","E","F","F#","G","G#"}[(key+2*12+3)%12]+str(key/12-2); }
inline String str(const Note& a) { return strKey(a.key); }
inline String str(const Sign& o) {
	/***/ if(o.type==Sign::Note) return str(o.note)+ref<string>{"₀","₁"}[o.staff];
	else if(o.type==Sign::Measure) return " | "__;
	else return str(int(o.type));
}
