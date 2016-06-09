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
 TimeSignature timeSignature = {4,4};
 constexpr int staffCount = 1;
 Clef clefs[staffCount] = {{GClef,0}};

 insertSign({Sign::TimeSignature, 0, .timeSignature=timeSignature});
 for(uint staff: range(staffCount)) insertSign({Sign::Clef, 0, {{staff, {.clef=clefs[staff]}}}});

 const uint staff = 0;
 uint time = 0;
 int nextFingering = 0;
 uint measureIndex = 0;
 map<int, int> measureAlterations; // Currently altered steps (for implicit alterations)

 while(s) {
  if(s.match(" ")) {}
  else if(s.match("\n")) {
   assert_(tuplet.size==1);
   measureAlterations.clear();
   bool pageBreak = s.match("\n") ? true : false;
   log(measureIndex+1, "|", time);
   assert_(time%(4*12)==0, time-(time/(4*12)*(4*12)), 4*12, 4*12-(time-(time/(4*12)*(4*12))));
   insertSign({Sign::Measure, time, .measure={pageBreak?Measure::PageBreak:Measure::NoBreak, measureIndex, 1, 1, measureIndex}});
   measureIndex++;
  }
  else if(s.match("[")) {
   assert_(tuplet.size==1);
   tuplet.size = 3;
  }
  else if(s.match("]")) {
   assert_(tuplet.size>1);
   assert_(tuplet.size == tupletCurrentSize, tuplet.size, tupletCurrentSize);
   log("T", time);
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
  else if(s.isInteger()) nextFingering = s.integer();
  else { // Chord
   int start = s.index;
   int minDuration = -1;
   for(;;) {
    int step = "CDEFGABcdefgab"_.indexOf(s.peek());
    if(step == -1) break;
    s.advance(1);
    Accidental accidental = Accidental::None;
    /**/  if(s.match("♭")) accidental = Accidental::Flat;
    else if(s.match("♮")) accidental = Accidental::Natural;
    else if(s.match("♯")) accidental = Accidental::Sharp;
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
   assert_(s.wouldMatchAny(" \n]"), s.line());
   if(tuplet.size>1) tupletCurrentSize++;
   log(s.sliceRange(start, s.index), minDuration);
   time += minDuration;
   //nextFingering = 0;
  }
 }
 toRelative(signs);
 convertAccidentals(signs);
 assert_(signs.last().type==Sign::Measure);
}
