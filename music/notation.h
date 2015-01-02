#pragma once
/// notation.h Music notation definitions
#include "string.h"

inline bool isPowerOfTwo(uint v) { return !(v & (v - 1)); }

struct PitchClass {
	char keyIntervals[11 +1];
	char accidentals[12 +1];
};
static constexpr PitchClass pitchClasses[14] = {
    //*7%12 //FIXME: generate
   // C♯D♯EF♯G♯A♯B   C♯D♯EF♯G♯A♯B
    {"10101101010", "N-N-NN-N-N-N"},  // ♭♭♭♭♭♭♭/♯♯♯♯♯ B g♯
    {"10101101010", "N-N-N.-N-N-N"}, // ♭♭♭♭♭♭/♯♯♯♯♯♯ G♭/F♯ e♭/d♯
	{"10101101010", ".-N-N.-N-N-N"}, // ♭♭♭♭♭/♯♯♯♯♯♯♯ D♭ b♭
	{"10101101010", ".-N-N.b.-N-N"}, // ♭♭♭♭ A♭ f
	{"10101101010", ".b.-N.b.-N-N"}, // ♭♭♭ E♭ c
	{"10101101010", ".b.-N.b.b.-N"}, // ♭♭ B♭ g
	{"10101101010", ".b.b..b.b.-N"}, // ♭ F d
  //{"10101101010", ".b.b..b.b.b."},  // ♮ C a
   // C♯D♯EF♯G♯A♯B   C♯D♯EF♯G♯A♯B
    {"01011010101", ".#.#..#.#.#."},  // ♮ C a
    {"01011010101", ".#.#.N+.#.#."},  // ♯ G e
	{"01011010101", "N+.#.N+.#.#."},  // ♯♯ D b
	{"01011010101", "N+.#.N+N+.#."},  // ♯♯♯ A f♯
	{"01011010101", "N+N+.N+N+.#."},  // ♯♯♯♯ E c♯
    {"01011010101", "N+N+.N+N+N+."},  // ♯♯♯♯♯/♭♭♭♭♭♭♭ B g♯
    {"01011010101", "N+N+NN+N+N+."}  // ♯♯♯♯♯♯/♭♭♭♭♭♭ F♯/G♭ d♯/e♭
};
inline int keyStep(int fifths, int key) {
    assert_(fifths >= -7 && fifths <= 6, fifths);
    int h=key/12*7; for(int i: range(key%12)/*0-10*/) h+=pitchClasses[fifths+7].keyIntervals[i]-'0';
	return h - 35; // 0 = C4;
}

namespace SMuFL { //Standard Music Font Layout
	namespace NoteHead { enum { Double=0xE0A0, Square, Whole, Half, Black }; }
	namespace Rest { enum { Maxima = 0xE4E0, Longa, Double, Whole, Half, Quarter, Eighth, Sixteenth, Thirtysecond, Sixtyfourth }; }
	namespace Clef {  enum { G=0xE050, F=0xE062 }; enum { _15mb=1, _8vb, _8va, _15ma }; }
	namespace TimeSignature {  enum { _0=0xE080 }; }
	enum { Dot=0xE1E7 };
	namespace Flag { enum { Above=0xE240, Below }; }
    enum Accidental { None=0, AccidentalBase=0xE260, Flat=AccidentalBase, Natural, Sharp, DoubleSharp, DoubleFlat, TripleSharp, TripleFlat, NaturalFlat, NaturalSharp, SharpSharp};
	static constexpr string accidental[] = {"flat"_,"natural"_,"sharp"_,"double-sharp"_,"double-flat"_};
    namespace Articulation { enum { Base=0xE4A0, Accent=0, Staccato=1, Tenuto=2 }; }
	enum Dynamic { DynamicBase=0xE520/*Piano=DynamicBase, Mezzo, Forte, Rinforzando, Sforzando, z, n, pppppp, ppppp, pppp, ppp, pp, mp, mf, pf, ff, fff, ffff, fffff, ffffff,
				   fp, fz, sf, sfp, sfpp, sfz, sfzp, sffz, rf, rfz*/ };
	static constexpr string dynamic[] = {
		"p", "m", "f", "r", "s", "z", "n", "pppppp", "ppppp", "pppp", "ppp", "pp", "mp", "mf", "pf", "ff", "fff", "ffff", "fffff", "ffffff",
		"fp", "fz", "sf", "sfp", "sfpp", "sfz", "sfzp", "sffz", "rf", "rfz"};
	namespace Pedal { enum { Mark = 0xE650 }; }
}

enum Value { InvalidValue=-1,                  /*Maxima, Longa, Double,*/ Whole, Half, Quarter, Eighth, Sixteenth, Thirtysecond, Sixtyfourth };
static constexpr string valueNames[] = {/*"maxima","longa","double"_,*/"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_,"32nd"_,"64th"_};
static constexpr uint valueDurations[] = {/*512, 256, 128,*/       64,         32,       16,           8,             4,         2,         1};
static constexpr uint quarterDuration = 16;

enum ClefSign { NoClef=0, FClef=SMuFL::Clef::F, GClef=SMuFL::Clef::G };
struct Clef {
	ClefSign clefSign;
	int octave;
};
enum OctaveShift { Down, Up, OctaveStop };

using Accidental = SMuFL::Accidental;
inline int keyAlteration(int fifths, int key) {
    assert_(fifths >= -7 && fifths <= 6 && key>0, fifths, key);
    char c = pitchClasses[fifths+7].accidentals[key%12/*0-11*/];
	if(c== 'b' || c=='-') return -1;
	if(c=='N' || c=='.') return 0;
	if(c=='#' || c=='+') return 1;
	error(c);
}
inline Accidental alterationAccidental(int alteration) {
    assert_(alteration >= -3 && alteration <= 5, alteration);
    return ref<Accidental>{Accidental::TripleFlat, Accidental::DoubleFlat, Accidental::Flat, Accidental::Natural,
                Accidental::Sharp, Accidental::DoubleSharp, Accidental::TripleSharp, Accidental::NaturalSharp, Accidental::SharpSharp}[alteration+3];
}
inline int accidentalAlteration(Accidental accidental) {
    size_t index = accidental - Accidental::AccidentalBase;
    assert_(index < 10, index);
    return ref<int>{-1,0,1,2,-2,3,-3,-4,4,5}[index];
}

// Converts note (octave, step, alteration) to MIDI key
inline int noteKey(int octave, int step, int alteration) {
    //assert_(octave==0);
    int stepOctave = step>=0 ? step/7 : (step-6)/7; // Rounds towards negative
    octave += stepOctave;
    int octaveStep = (step - step/7*7 + 7)%7; // signed step%7 (Step offset on octave scale)
    assert_(stepOctave*7 + octaveStep == step);
    // C [C#] D [D#] E F [F#] G [G#] A [A#] B
	return 60 + octave*12 + ref<uint>{0,2,4,5,7,9,11}[octaveStep] + alteration;
}

inline int signatureAlteration(int keySignature, int step) {
	int octaveStep = (step - step/7*7 + 7)%7; // signed step%7 (Step offset on octave scale)
	for(int i: range(abs(keySignature))) { // FIXME: closed form ?
		int fifthStep = keySignature<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
		if(octaveStep == fifthStep%7) return keySignature>0 ? 1 : -1;
	}
	return 0;
}

struct Note {
	Value value; // FIXME: -> ../union
	Clef clef; // Explicit current clef context for convenience
	int step; // Independent from clef (0 = C4)
	int alteration;
	Accidental accidental;
    enum Tie { NoTie, TieStart, TieContinue, TieStop, Merged } tie;
	uint durationCoefficientNum /* Tuplet duration */, durationCoefficientDen /* Tuplet note count */;
    bool dot;// = false;
    bool grace;// = false;
    bool acciaccatura;// = false; // Before principal beat (slashed)
    bool accent;// = false;
    bool staccato;// = false;
    bool tenuto;// = false;
    bool trill;// = false;
    int finger;// = 0;
	//bool stem:1; // 0: undefined, 1: down, 2: up
    size_t pageIndex/* = invalid*/, measureIndex/* = invalid*/, glyphIndex/* = invalid*/, accidentalGlyphIndex/* = invalid*/;
    int tieStartNoteIndex/* = 0*/; // Invalidated by any insertion/deletion
    float accidentalOpacity/* = 1*/;

    uint key() const { return noteKey(clef.octave, step, alteration); }
	uint duration() const { // in .16 beat units
		uint duration = valueDurations[value];
		if(dot) duration = duration * 3 / 2;
		duration = duration * durationCoefficientNum / durationCoefficientDen;
		return duration;
	};
};
struct Rest {
	Value value;
	uint duration() const { return valueDurations[value]; };
};
struct Measure {
	enum Break { NoBreak, LineBreak, PageBreak };
	Break lineBreak; uint measure, page, pageLine, lineMeasure;
};
enum class Repeat { Begin=-2, End=-1, None=0 };
typedef int KeySignature; // Index on the fifths circle
struct TimeSignature { uint beats, beatUnit; };
struct Metronome { Value beatUnit; uint perMinute; };
struct Step { uint staff; int step; };
struct Tuplet { uint size; struct { uint time; Step min, max; } first, last; Step min, max; };
using Dynamic = SMuFL::Dynamic;
enum Wedge { Crescendo, Diminuendo, WedgeStop };
enum Pedal { Start, Change, PedalStop, Ped };

struct Sign {
    enum {
		Invalid,
        Clef, OctaveShift,
		Measure, Repeat, KeySignature, TimeSignature, Metronome,
		Note, Rest,
		Tuplet,
		Dynamic, Wedge,
		Pedal
	} type;
	uint time; // Absolute time offset
	union {
		struct {
			uint staff;
			union {
				struct {
					int duration; // in ticks
					//Value value;
					union {
						::Note note;
						::Rest rest;
					};
				};
				::Clef clef;
				::OctaveShift octave;
			};
		};
		::Measure measure;
		::KeySignature keySignature;
		::TimeSignature timeSignature;
		::Repeat repeat;
		::Tuplet tuplet;
		::Metronome metronome;
		::Dynamic dynamic;
		::Wedge wedge;
		::Pedal pedal;
	};
};
//static_assert(sizeof(Sign)<=112/*40*/, "");

inline bool operator <(const Sign& a, const Sign& b) {
    if(a.time==b.time) {
		if(a.type==Sign::Note && b.type==Sign::Note) return a.note.step < b.note.step;
        if(a.type==Sign::Measure && b.type==Sign::Repeat) return b.repeat != Repeat::End;
        if(a.type==Sign::Measure && b.type==Sign::Pedal) return b.pedal != Pedal::PedalStop;
        return a.type < b.type;
    }
	return a.time < b.time;
}

inline String superDigit(int digit) {
	assert_(abs(digit) <= 9); return (digit>0?""_:"⁻"_)+ref<string>{"⁰"_,"¹"_, "²"_, "³"_, "⁴"_, "⁵"_, "⁶"_, "⁷"_, "⁸"_, "⁹"_}[abs(digit)];
}
inline String strKey(int key) {
    assert_(key>0); return (string[]){"A"_,"A♯"_,"B"_,"C"_,"C♯"_,"D"_,"D♯"_,"E"_,"F"_,"F♯"_,"G"_,"G♯"_}[(key+2*12+3)%12]+superDigit(key/12-2);
}
inline String strNote(int octave, int step, Accidental accidental) {
	octave += /*lowest A-1*/3 + (step>0 ? step/7 : (step-6)/7); // Rounds towards negative
	int octaveStep = (step - step/7*7 + 7)%7; // signed step%7 (Step offset on octave scale)
	return "CDEFGAB"_[octaveStep]+(accidental?ref<string>{"♭"_,"♮","♯"_}[accidental-Accidental::AccidentalBase]:""_)+superDigit(octave);
}
inline String str(const Note& o) { return strNote(o.clef.octave, o.step, o.accidental); }
inline String str(const Clef& o) {
	if(o.clefSign==ClefSign::GClef) return "G:"__;
	if(o.clefSign==ClefSign::FClef) return "F:"__;
	error("");
}
inline String str(const Sign& o) {
	if(o.type==Sign::Clef || o.type==Sign::OctaveShift || o.type==Sign::Note || o.type==Sign::Rest) {
		String s;
		if(o.type==Sign::Clef) s = str(o.clef);
		else if(o.type==Sign::OctaveShift) s = copyRef(ref<string>{"8va"_,"8vb"_,"⸥"_}[int(o.octave)]);
		else if(o.type==Sign::Note) s = str(o.note);
		else if(o.type==Sign::Rest) s = copyRef(str("-;,"_[clip(0, int(o.rest.value)-Value::Whole, 1)]));
		else error(int(o.type));
        assert_(o.staff == 0 || o.staff == 1);
		return s + ref<string>{"₀","₁"}[o.staff];
	}
	if(o.type==Sign::Measure) return " | "__;
    if(o.type==Sign::Repeat) return " : "__;
    if(o.type==Sign::KeySignature) return o.keySignature ? repeat(o.keySignature>0?"♯"_:"♭"_,abs(o.keySignature)) : "♮"__;
	if(o.type==Sign::TimeSignature) return str(o.timeSignature.beats)+"/"_+str(o.timeSignature.beatUnit);
	if(o.type==Sign::Tuplet) return "}"_+str(o.tuplet.size);
	if(o.type==Sign::Metronome) { assert_(o.metronome.beatUnit==Quarter); return "♩="_+str(o.metronome.perMinute); }
	if(o.type==Sign::Dynamic) return copyRef(SMuFL::dynamic[o.dynamic-Dynamic::DynamicBase]);
	if(o.type==Sign::Wedge) return copyRef(ref<string>{"<"_, ">", ""_}[int(o.wedge)]);
    if(o.type==Sign::Pedal) return copyRef(ref<string>{"\\"_, "^"_, "⌋"_, "P"_}[int(o.pedal)/*-Ped*/]);
	error(int(o.type));
}
