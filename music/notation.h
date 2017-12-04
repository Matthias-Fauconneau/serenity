#pragma once
/// \file notation.h Music notation definitions
#include "string.h"
#include "vector.h"

inline bool isPowerOfTwo(uint v) { return !(v & (v - 1)); }

static constexpr char accidentals[14][12+1] = {
    //*7%12 //FIXME: generate
    // C♯D♯EF♯G♯A♯B   C♯D♯EF♯G♯A♯B
    {"N-N-NN-N-N-N"},  // ♭♭♭♭♭♭♭/♯♯♯♯♯ B g♯
    {"N-N-N.-N-N-N"}, // ♭♭♭♭♭♭/♯♯♯♯♯♯ G♭/F♯ e♭/d♯
    {".-N-N.-N-N-N"}, // ♭♭♭♭♭/♯♯♯♯♯♯♯ D♭ b♭
    {".-N-N.b.-N-N"}, // ♭♭♭♭ A♭ f
    {".b.-N.b.-N-N"}, // ♭♭♭ E♭ c
    {".b.-N.b.b.-N"}, // ♭♭ B♭ g
    {".b.b..b.b.-N"}, // ♭ F d
    {".b.b..b.b.b."},  // ♮ C a (HACK)
    // C♯D♯EF♯G♯A♯B   C♯D♯EF♯G♯A♯B
    //{".#.#..#.#.#."},  // ♮ C a (FIXME)
    {".#.#.N+.#.#."},  // ♯ G e
    {"N+.#.N+.#.#."},  // ♯♯ D b
    {"N+.#.N+N+.#."},  // ♯♯♯ A f♯
    {"N+N+.N+N+.#."},  // ♯♯♯♯ E c♯
    {"N+N+.N+N+N+."},  // ♯♯♯♯♯/♭♭♭♭♭♭♭ B g♯
    {"N+N+NN+N+N+."}  // ♯♯♯♯♯♯/♭♭♭♭♭♭ F♯/G♭ d♯/e♭
};
inline int keyStep(int fifths, int key) {
    assert_(fifths >= -7 && fifths <= 6, fifths);
    int h=key/12*7; for(int i: range(key%12)/*0-10*/) h+=(fifths<=/*!!!*/0?"10101101010"_:"01011010101"_)[i]-'0';
    return h - 35; // 0 = C4;
}

namespace SMuFL { //Standard Music Font Layout
namespace NoteHead { enum { Breve=0xE0A1, Whole, Half, Black }; }
namespace Rest { enum { Maxima = 0xE4E0, Longa, Double, Whole, Half, Quarter, Eighth, Sixteenth, Thirtysecond, Sixtyfourth }; }
namespace Clef {  enum { G=0xE050, F=0xE062 }; enum { _15mb=1, _8vb, _8va, _15ma }; }
namespace TimeSignature {  enum { _0=0xE080 }; }
enum { Dot=0xE1E7 };
enum Tremolos { Tremolo };
namespace Flag { enum { Above=0xE240, Below }; }
enum Accidental { None=0, AccidentalBase=0xE260, Flat=AccidentalBase, Natural, Sharp, DoubleSharp, DoubleFlat, TripleSharp, TripleFlat, NaturalFlat, NaturalSharp, SharpSharp};
static constexpr string accidental[] = {"flat"_,"natural"_,"sharp"_,"double-sharp"_,"flat-flat"_/*double-flat*/};
namespace Articulation { enum { Base=0xE4A0, Accent=0, Staccato=1, Tenuto=2 }; }
enum Dynamic { DynamicBase=0xE520/*Piano=DynamicBase, Mezzo, Forte, Rinforzando, Sforzando, z, n, pppppp, ppppp, pppp, ppp, pp, mp, mf, pf, ff, fff, ffff, fffff, ffffff,
                                              fp, fz, sf, sfp, sfpp, sfz, sfzp, sffz, rf, rfz*/ };
static constexpr string dynamic[] = {
    "p", "m", "f", "r", "s", "z", "n", "pppppp", "ppppp", "pppp", "ppp", "pp", "mp", "mf", "pf", "ff", "fff", "ffff", "fffff", "ffffff",
    "fp", "fz", "sf", "sfp", "sfpp", "sfz", "sfzp", "sffz", "rf", "rfz"};
enum Ornaments { SlashUp = 0xE564, SlashDown };
namespace Pedal { enum { Mark = 0xE650 }; }
enum Segment { Arpeggio = 0xEAA9 };
enum Fretboard { FilledCircle=0xE858 };
}

enum Value { InvalidValue=-1, Long, Breve, Whole, Half, Quarter, Eighth, Sixteenth, Thirtysecond, Sixtyfourth };
static constexpr string valueNames[] = {"long", "breve"_, "whole"_,"half"_, "quarter"_, "eighth"_, "16th"_, "32nd"_, "64th"_};
static constexpr uint valueDurations[] = {256, 128,       64,         32,       16,           8,             4,         2,         1};
static constexpr uint quarterDuration = 16;

enum ClefSign { NoClef=0, FClef=SMuFL::Clef::F, GClef=SMuFL::Clef::G };
struct Clef {
    ClefSign clefSign = GClef;
    int octave = 0;
};
enum OctaveShift { Down, Up, OctaveStop };

using Accidental = SMuFL::Accidental;
inline int keyAlteration(int fifths, int key) {
    assert_(fifths >= -7 && fifths <= 6 && key>0, fifths, key);
    char c = accidentals[fifths+7][key%12];
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
    assert_(index < 10, index, hex(accidental));
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
    uint durationCoefficientNum = 0 /* Tuplet duration */, durationCoefficientDen = 0 /* Tuplet note count */;
    bool dot = false;
    bool grace = false;
    bool acciaccatura = false; // Before principal beat (slashed)
    bool accent = false;
    bool staccato = false;
    bool tenuto = false;
    bool trill = false;
    enum Tremolo { NoTremolo, Start=1, Stop=2 } tremolo = NoTremolo;
    bool arpeggio = false;
    int finger = 0;
    //bool stem:1; // 0: undefined, 1: down, 2: up
    size_t pageIndex = invalid, measureIndex = invalid, glyphIndex[4] = {invalid, invalid, invalid, invalid};
    size_t signIndex = invalid;
    int tieStartNoteIndex = 0; // Invalidated by any insertion/deletion
    float accidentalOpacity = 1;
    int string, fret;

    uint key() const { return noteKey(clef.octave, step, alteration); }
    uint duration() const { // in .16 beat units
        uint duration = valueDurations[value];
        if(dot) duration = duration * 3 / 2;
        assert_(durationCoefficientDen);
        duration = duration * durationCoefficientNum / durationCoefficientDen;
        return duration;
    };
};
struct Rest {
    Value value;
    bool dot = false;
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
struct ChordName { char name[24]={}; };
struct ChordBox { int top=0; char name[24]={}; };
struct Tuplet { uint size; struct { int min, max; } first, last; int min, max; };
using Dynamic = SMuFL::Dynamic;
enum Wedge { Crescendo, Diminuendo, WedgeStop };
enum Pedal { Start, Change, PedalStop, Ped };

struct Sign {
    enum {
        Invalid,
        Clef, OctaveShift,
        Measure, Repeat, KeySignature, TimeSignature, Metronome, ChordName, ChordBox,
        Note, Rest,
        Tuplet,
        Dynamic, Wedge,
        Pedal,
    } type;
    uint64 time; // Absolute time offset in ticks (/sa ticksPerQuarter)
    union {
        struct {
            uint staff;
            union {
                struct {
                    /*u*/int64 duration;
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
        ::ChordName chordName;
        ::ChordBox chordBox;
        ::Dynamic dynamic;
        ::Wedge wedge;
        ::Pedal pedal;
    };
};
//static_assert(sizeof(Sign)<=112/*40*/, "");

inline bool operator ==(const Note& a, const Note& b) {
 return a.key() == b.key();
}
inline bool operator ==(const Sign& a, const Sign& b) {
 assert_(a.type==Sign::Note && b.type==Sign::Note);
 return a.staff == b.staff && a.note == b.note;
}
inline bool operator <(const Sign& a, const Sign& b) {
    if(a.time==b.time) {
     if(a.type==Sign::Note && b.type==Sign::Note && (a.note.string||b.note.string)) return a.note.string < b.note.string;
     if(a.type==Sign::Note && b.type==Sign::Note) return a.note.step < b.note.step;
        if(a.type==Sign::Measure && b.type==Sign::Repeat) return b.repeat != Repeat::End;
        if(a.type==Sign::Measure && b.type==Sign::Pedal) return b.pedal != Pedal::PedalStop;
        return a.type < b.type;
    }
    return a.time < b.time;
}

inline String superDigit(int digit) {
    assert_(abs(digit) <= 9); return (digit>=0?""_:"⁻"_)+ref<string>{"⁰"_,"¹"_, "²"_, "³"_, "⁴"_, "⁵"_, "⁶"_, "⁷"_, "⁸"_, "⁹"_}[abs(digit)];
}
inline String strKey(int fifths, int key) {
    assert_(key>0);
    //return (string[]){"A"_,"A♯"_,"B"_,"C"_,"C♯"_,"D"_,"D♯"_,"E"_,"F"_,"F♯"_,"G"_,"G♯"_}[(key+2*12+3)%12]
    /*+superDigit(key/12-2)*/;
    const int step = keyStep(fifths, key)+37;
    const int octave = key/12-2; //*lowest A-1*/3 + (step>0 ? step/7 : (step-6)/7); // Rounds towards negative
    const int alt = keyAlteration(fifths, key)+1;
    assert_(alt >= 0 && alt <= 2);
    return char('A'+step%7)+ref<string>{"♭"_,""_,"♯"_}[alt]+superDigit(octave);
    //return char('A'+step%7)+ref<string>{"b"_,""_,"#"_}[alt]+str(octave);
}
inline String strNote(const int octaveBias, const int step, Accidental accidental) {
    //assert_(step >= 0);
    int octave = octaveBias + (step>0 ? step/7 : (step-6)/7); // Rounds towards negative
    //assert_(octave >= 0, octave, octaveBias, step);
    int octaveStep = (step - step/7*7 + 7)%7; // signed step%7 (Step offset on octave scale)
    array<char> s;
    s.append("CDEFGABcdefgab"_[min(1,octave)*7+octaveStep]);
    if(accidental) s.append(ref<string>{"♭"_,"♮","♯"_}[accidental-Accidental::AccidentalBase]);
    s.append(repeat("'"_, max(0, octave-1))); //superDigit(octave);
    return ::move(s);
    //return "CDEFGAB"_[octaveStep]+(accidental?ref<string>{"B"_,"N"_,"#"_}[accidental-Accidental::AccidentalBase]:""_)+str(octave);
}
inline String str(const Note& o) {
 array<char> s = strNote(o.clef.octave, o.step, o.accidental);
 /**/ if(o.value == Whole) s.append('O');
 else if(o.value == Half) s.append('o');
 else if(o.value == Quarter) s.append('-');
 else if(o.value == Eighth) {}
 else if(o.value == Sixteenth) s.append(';');
 else error(int(o.value));
 return ::move(s);
}
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
        //else if(o.type==Sign::Note) s = strKey(0, o.note.key()); //str(o.note);
        else if(o.type==Sign::Note) s = str(o.note);
        else if(o.type==Sign::Rest) {
         assert_(o.rest.value >= Quarter && o.rest.value <= Sixteenth);
         s = copyRef(str(":,;"_[clamp(0, int(o.rest.value)-Value::Quarter, 2)]));
         //log(int(o.rest.value), s);
        }
        else error(int(o.type));
        assert_(o.staff <= 9);
        return s;// + ref<string>{"₀","₁","₂","₃","₄","₅","₆","₇","₈","₉"}[o.staff];
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

inline int insertSign(array<Sign>& signs, array<int>& activeTies, int& minStep, int& maxStep, Tuplet& tuplet, Sign sign) {
    int signIndex = signs.insertSorted(sign);
    for(int& index: activeTies) if(signIndex <= index) index++;
    if(signIndex <= minStep) minStep++;
    if(signIndex <= maxStep) maxStep++;
    for(Sign& sign: signs) {
        if(sign.type == Sign::Tuplet) {
            Tuplet& tuplet = sign.tuplet;
            if(signIndex <= tuplet.first.min) tuplet.first.min++;
            if(signIndex <= tuplet.first.max) tuplet.first.max++;
            if(signIndex <= tuplet.last.min) tuplet.last.min++;
            if(signIndex <= tuplet.last.max) tuplet.last.max++;
            if(signIndex <= tuplet.min) tuplet.min++;
            if(signIndex <= tuplet.max) tuplet.max++;
        }
    }
    if(signIndex <= tuplet.first.min) tuplet.first.min++;
    if(signIndex <= tuplet.first.max) tuplet.first.max++;
    if(signIndex <= tuplet.last.min) tuplet.last.min++;
    if(signIndex <= tuplet.last.max) tuplet.last.max++;
    if(signIndex <= tuplet.min) tuplet.min++;
    if(signIndex <= tuplet.max) tuplet.max++;
    return signIndex;
};

inline void toRelative(mref<Sign> signs) {
 // Converts absolute references to relative references (tuplet)
 for(int signIndex: range(signs.size)) {
  Sign& sign = signs[signIndex];
  if(sign.type == Sign::Tuplet) {
   Tuplet& tuplet = sign.tuplet;
   tuplet.first.min = tuplet.first.min - signIndex;
   tuplet.first.max = tuplet.first.max - signIndex;
   tuplet.last.min = tuplet.last.min - signIndex;
   tuplet.last.max = tuplet.last.max - signIndex;
   tuplet.min = tuplet.min - signIndex;
   tuplet.max = tuplet.max - signIndex;
   //assert_(tuplet.first.min<0&&tuplet.first.max<0&&tuplet.last.min<0&&tuplet.last.max<0&&tuplet.min<0&&tuplet.max<0);
  }
 }
}

#include "map.h"
inline int implicitAlteration(int keySignature, const map<int, int>& measureAlterations, int step) {
    return measureAlterations.contains(step) ? measureAlterations.at(step) : signatureAlteration(keySignature, step);
}

// Converts accidentals to match key signature (pitch class). Tie support needs explicit tiedNoteIndex to match ties while editing steps
inline void convertAccidentals(mref<Sign> signs) {
 KeySignature keySignature = 0;
 size_t measureStartIndex=0;
 map<int, int> previousMeasureAlterations; // Currently accidented steps (for implicit accidentals)
 for(size_t signIndex : range(signs.size)) {
  map<int, int> measureAlterations; // Currently accidented steps (for implicit accidentals)
  map<int, int> sameAlterationCount; // Alteration occurence count
  for(size_t index: range(measureStartIndex, signIndex)) {
   const Sign sign = signs[index];
   if(sign.type == Sign::Note) {
    sameAlterationCount[sign.note.step]++;
    if(sign.note.accidental && measureAlterations[sign.note.step] != accidentalAlteration(sign.note.accidental)) {
     measureAlterations[sign.note.step] = accidentalAlteration(sign.note.accidental);
     sameAlterationCount[sign.note.step] = 0;
    }
   }
  }

  Sign& sign = signs[signIndex];
  if(sign.type == Sign::Measure) { measureStartIndex = signIndex; previousMeasureAlterations = move(measureAlterations); }
  if(sign.type == Sign::KeySignature) keySignature = sign.keySignature;
  if(sign.type == Sign::Note) {
   if(sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop)  {
    if(sign.note.tieStartNoteIndex) {
     assert_(sign.note.tieStartNoteIndex);
     assert_(signs[sign.note.tieStartNoteIndex].type == Sign::Note && (
        signs[sign.note.tieStartNoteIndex].note.tie == Note::TieStart
       || signs[sign.note.tieStartNoteIndex].note.tie == Note::TieContinue), sign,
       sign.note.tieStartNoteIndex, signs[sign.note.tieStartNoteIndex]);
     sign.note.step = signs[sign.note.tieStartNoteIndex].note.step;
     sign.note.alteration = signs[sign.note.tieStartNoteIndex].note.alteration;
     if(sign.note.accidental) log("sign.note.accidental");
     continue;
    }
   }

   auto measureAccidental = [&](int step, int alteration) {
    return (alteration == implicitAlteration(keySignature, measureAlterations, step)
            && (!measureAlterations.contains(step) || sameAlterationCount[step] > 1) // Repeats measure alterations once
            && alteration == previousMeasureAlterations.value(step, alteration)) // Courtesy accidental
      //&& TODO: courtesy accidentals for white to white key alteration (Cb, Fb, B#, E#)
      ? Accidental::None :
        alterationAccidental(alteration);
   };
   auto courtesyAccidental = [&](int step, int alteration) {
    return alteration == implicitAlteration(keySignature, measureAlterations, step)
      && ((measureAlterations.contains(step) && sameAlterationCount[step] <= 1) // Repeats measure alterations once
          || alteration != previousMeasureAlterations.value(step, alteration)); // Courtesy accidental
   };

   // Recomputes accidental to reflect any previous changes to implicit alterations in the same measure
   sign.note.accidental = measureAccidental(sign.note.step, sign.note.alteration);
   sign.note.accidentalOpacity = courtesyAccidental(sign.note.step, sign.note.alteration) ? 1./2 : 1;

   int key = sign.note.key();
   int step = keyStep(keySignature, key) - sign.note.clef.octave*7;
   int alteration = keyAlteration(keySignature, key);
   Accidental accidental = measureAccidental(step, alteration);

   //assert(!sign.note.clef.octave, sign.note.clef.octave, sign.note.step);
   assert_(key == noteKey(sign.note.clef.octave, step, alteration),
           keySignature, sign.note, sign.note.clef.octave, sign.note.step, sign.note.alteration, key, step, alteration, noteKey(sign.note.clef.octave, step, alteration),
           strNote(0, step, accidental)/*, strKey(101), strKey(113)*/);

   if((accidental && !sign.note.accidental) // Does not introduce additional accidentals (for ambiguous tones)
      || (accidental && sign.note.accidental  // Restricts changes to an existing accidental ...
          && (sign.note.accidental < accidental) == (keySignature < 0) // if already aligned with key alteration direction
          && sign.note.accidental < Accidental::DoubleSharp)) { // and if simple accidental (explicit complex accidentals)
    if(previousMeasureAlterations.contains(sign.note.step)) previousMeasureAlterations.remove(sign.note.step); // Do not repeat courtesy accidentals
    continue;
   }
   sign.note.step = step;
   sign.note.alteration = alteration;
   sign.note.accidental = accidental;
   sign.note.accidentalOpacity = courtesyAccidental(sign.note.step, sign.note.alteration) ? 1./2 : 1;
   if(previousMeasureAlterations.contains(step)) previousMeasureAlterations.remove(step); // Do not repeat courtesy accidentals
  }
 }
}
