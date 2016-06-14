#include "abc.h"
#include "data.h"

ABC::ABC(ref<byte> file) {
 TextData s(file);

 int minStep = 0, maxStep = 0;
 Tuplet tuplet {1, {0,0}, {0,0}, 0,0}; //{0,{},{},{},{}};
 uint tupletCurrentSize = 0;
 array<int> activeTies;
 auto insertSign = [this,&activeTies,&minStep,&maxStep,&tuplet](Sign sign) { return ::insertSign(signs, activeTies, minStep, maxStep, tuplet, sign); };

 KeySignature keySignature = 0;
 TimeSignature timeSignature = {0,0};
 constexpr int staffCount = 1;
 Clef clefs[staffCount] = {{GClef,0}};

 const uint staff = 0;
 uint time = 0;
 int nextFingering = 0;
 uint measureIndex = 0;
 map<int, int> measureAlterations; // Currently altered steps (for implicit alterations)

 while(s) {
  assert_(!s.match('/'), nextFingering);
  if(s.match(" ")||s.match("\t")) {}
  else if(s.match("\n")) {
   assert_(tuplet.size==1);
   measureAlterations.clear();
   bool pageBreak = s.match("\n") ? true : false;
   //log(measureIndex+1, "|", time);
   assert_(time%(4*12)==0, time-(time/(4*12)*(4*12)), 4*12, 4*12-(time-(time/(4*12)*(4*12))), s.data.slice(0, s.index), "|&^|", s.data.slice(s.index, 16));
   insertSign({Sign::Measure, time, .measure={pageBreak?Measure::PageBreak:Measure::NoBreak, measureIndex, 1, 1, measureIndex}});
   measureIndex++;
  }
  else if(s.match("|")) {
   int top = s.integer();
   s.skip(' ');
   string name = s.until('|');
   ChordBox chord {top};
   mref<char>(chord.name).slice(0,name.size).copy(name);
   mref<char>(chord.name).slice(name.size).clear(0);
   insertSign({Sign::ChordBox, time, .chordBox=chord});
  }
  else if(s.match("[")) {
   assert_(tuplet.size==1);
   tuplet.size = 3;
  }
  else if(s.match("]")) {
   assert_(tuplet.size>1);
   assert_(tuplet.size == tupletCurrentSize, tuplet.size, tupletCurrentSize);
   //log("T", time);
   insertSign({Sign::Tuplet, signs[tuplet.last.max].time, {.tuplet=tuplet}});
   tuplet = {1, {0,0}, {0,0}, 0,0};
   tupletCurrentSize = 0;
  }
  else if(s.match(";")) {
   insertSign({Sign::Rest, uint64(time), {{staff, {{3, .rest={Sixteenth}}}}}});
   time += 3;
  }
  else if(s.match(",")) {
   insertSign({Sign::Rest, uint64(time), {{staff, {{6, .rest={Eighth}}}}}});
   time += 6;
  }
  else if(s.match(":")) {
   insertSign({Sign::Rest, uint64(time), {{staff, {{12, .rest={Quarter}}}}}});
   time += 12;
  }
  else if(s.isInteger()) {
   int integer = s.integer();
   if(s.match("/")) { // Time Signature
    uint beats = integer;
    uint beatUnit = s.integer();
    s.match('\n');
    TimeSignature newTimeSignature {beats, beatUnit};
    if(newTimeSignature.beats != timeSignature.beats || newTimeSignature.beatUnit != timeSignature.beatUnit) {
     assert_(time == 0);
     timeSignature = newTimeSignature;
     insertSign({Sign::TimeSignature, 0, .timeSignature=timeSignature});
    }
   } else nextFingering = integer;
  }
  else if(s.match("♩=")) {
   uint perMinute = s.integer();
   s.match('\n');
   insertSign({Sign::Metronome, uint64(time), .metronome={Quarter, perMinute}});
  }
  else if(s.match("G:")) {
   s.match('\n');
   for(uint staff: range(staffCount)) insertSign({Sign::Clef, 0, {{staff, {.clef=clefs[staff]}}}});
  }
  else { // Chord
   //int start = s.index;
   int minDuration = -1;
   for(;;) {
    int step = "CDEFGABcdefgab"_.indexOf(s.peek());
    if(step == -1) break;
    int start = s.index;
    s.advance(1);
    Accidental accidental = Accidental::None;
    /**/  if(s.match("♭")) accidental = Accidental::Flat;
    else if(s.match("♮")) accidental = Accidental::Natural;
    else if(s.match("♯")) accidental = Accidental::Sharp;
    size_t point = s.index;
    for(;;) {
     if(s.match("/")) s.matchAny("CDEFGAB");
     else if(s.match("dim")||s.match("maj")||s.match("m")) {}
     else if(s.match("⁵")||s.match("⁶")||s.match("⁷")||s.match("⁹")||s.match("¹³")||s.match("♭")||s.match("♯")||s.match("⁽")||s.match("ᵇ")||s.match("⁺")||s.match("⁾")) {}
     else break;
    }
    if(s.index > point) { // Chord name
     string name = s.sliceRange(start,s.index);
     ChordName chord;
     mref<char>(chord.name).slice(0,name.size).copy(name);
     mref<char>(chord.name).slice(name.size).clear(0);
     insertSign({Sign::ChordName, time, .chordName=chord});
     goto continue2;
    }
    const int implicitAlteration = ::implicitAlteration(keySignature, measureAlterations, step);
    const int alteration = accidental ? accidentalAlteration(accidental): implicitAlteration;
    //if(alteration == implicitAlteration) accidental = Accidental::None;
    if(accidental) measureAlterations[step] = alteration;
    if(s.match("'")) step += 7;
    assert_(step >= 0);
    int duration = 6; Value value = Eighth;
    if(s.match(";")) { duration /= 2; value=Value(int(value)+1); }
    if(s.match("-")) { duration *= 2; value=Value(int(value)-1); }
    const bool dot = s.match(".") ? true : false;
    if(dot) duration = duration * 3 / 2;
    if(tuplet.size>1) duration = duration * (tuplet.size-1) / tuplet.size;
    int index = insertSign({Sign::Note, time, {{staff, {{duration, .note={
                                              .value = value,
                                              .clef = clefs[staff],
                                              .step = step,
                                              .alteration = alteration,
                                              .accidental = accidental,
                                              .tie = Note::NoTie,
                                              .durationCoefficientNum = tuplet.size>1 ? tuplet.size-1 : 1,
                                              .durationCoefficientDen = tuplet.size>1 ? tuplet.size : 1,
                                              .dot=dot,
                                              .finger=nextFingering
                                             }}}}}});
    minDuration = ::min<uint>(minDuration, duration);
    if(tuplet.size>1) {
     if(!tupletCurrentSize) {
      tuplet = {tuplet.size, {index,index}, {index,index}, index,index};
      //tupletCurrentSize = 1;
     } else {
      tuplet.last = {index,index};
      if(step < signs[tuplet.min].note.step) tuplet.min = index;
      if(step > signs[tuplet.max].note.step) tuplet.max = index;
     }
    }
    nextFingering = 0;
   }
   assert_(minDuration>0, s.line());
   assert_(s.wouldMatchAny(" \t\n]"), s.line());
   if(tuplet.size>1) tupletCurrentSize++;
   //log(s.sliceRange(start, s.index), minDuration);
   time += minDuration;
   //nextFingering = 0;
  }
  continue2:;
 }
 toRelative(signs);
 convertAccidentals(signs);
 assert_(signs.last().type==Sign::Measure);
}
