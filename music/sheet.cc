#include "sheet.h"
#include "notation.h"
#include "text.h"
#include "algorithm.h"
#include "sort.h"

static int clefStep(Clef clef, int step) { return step - (clef.clefSign==GClef ? 10 : -2) - clef.octave*7; } // Translates C4 step to top line step using clef
static int clefStep(Sign sign) { assert_(sign.type==Sign::Note, int(sign.type)); return clefStep(sign.note.clef, sign.note.step); }

static vec2 text(vec2 origin, string message, float fontSize, array<Glyph>& glyphs, vec2 unused align=0 /*0:right|top,1/2,center,1:left|bottom*/) {
 Text text(message, fontSize, 0, 1, 0, "LinLibertine", false);
 vec2 textSize = text.sizeHint();
 origin -= align*textSize;
 glyphs.append( text.glyphs(origin) );
 return textSize;
}

// Sheet parameters and musical context
struct SheetContext {
 // Musical context
 uint ticksPerSecond; // Divisions (tick time unit rhythmic definition)
 uint beatsPerMinute = 0; // Tempo
 TimeSignature timeSignature = {4,4};
 KeySignature keySignature = 0;
 vec2 pedalStart = 0; size_t pedalStartSystemIndex=0; // Last pedal start/change position
 Sign wedgeStart {.time=0, .wedge={}}; // Current wedge
 float wedgeStartX = 0;

 // Sheet layout parameters
 ///*static constexpr*/const size_t staffCount;
 float halfLineInterval, lineInterval = 2*halfLineInterval;
 float stemWidth = 0, stemLength = 7*halfLineInterval, beamWidth = halfLineInterval;
 float shortStemLength = 5*halfLineInterval;
 float margin = halfLineInterval;
 float textSize = 6*halfLineInterval;
 static constexpr bool tablature = false;
 float interstaffDistance = 14*lineInterval;

 // Tablature
 int handPosition = 9; // Lowest fret
 struct StringFret { int string, fret; };
 StringFret nextNote(Sign sign) {
  if(sign.note.finger) handPosition = sign.note.finger;
  return fingering(sign);
 }

 StringFret fingering(int key, int lowestFret) {
  static int strings[] = {52, 57, 62, 67, 71, 76}; //EADGBe
  int bestString = 0, bestFret = 0;
  for(int string: range(6)) {
   int fret = key - strings[string];
   if(fret < lowestFret) break;
   bestString = string;
   bestFret = fret;
  }
  //assert_(bestFret <= 16, bestFret);
  assert_(bestFret <= 19, bestFret);
  return {bestString, bestFret};
}
 StringFret fingering(Sign sign) {
  assert_(sign.type==Sign::Note, int(sign.type));
  assert_(tablature);
  return fingering(sign.note.key(), this->handPosition);
 }

 StringFret fingeringS(int key, int lowestFret, int highestFret) {
  static int strings[] = {52, 57, 62, 67, 71, 76}; //EADGBe
  for(int string: range(6)) {
   int fret = key - strings[string];
   if(fret >= lowestFret && fret <= highestFret) return {string, fret};
  }
  return {-1,-1};
}

 // Vertical positioning
 float staffY(int staff, int clefStep) { return -staff*10*lineInterval - clefStep * halfLineInterval; }
 float Y(uint staff, Clef clef, int step) { return staffY(staff, clefStep(clef, step)); };
 float Y(Sign sign) {
  assert_(sign.type==Sign::Note, int(sign.type));
  return staffY(sign.staff, clefStep(sign));
 }
 float tablatureY(Sign sign) {
  return -int(sign.staff)*10*lineInterval + interstaffDistance + (5-sign.note.string) * lineInterval;// - (sign.note.fret-5)/10.f * halfLineInterval;
 }

 SheetContext(uint ticksPerSecond, float halfLineInterval) : ticksPerSecond(ticksPerSecond), halfLineInterval(halfLineInterval) {}
};

// Signs belonging to a same chord (same time)
// Implicitly copyable array<Sign>
struct Chord : array<Sign> { Chord() {} Chord(ref<Sign> o) : array(::copyRef(o)) {} Chord(const Chord& o) : array(::copyRef(o)) {} };
// Implicitly copyable array<Chord>
struct Chords : array<Chord> { Chords() {} Chords(const Chords& o) : array(::copyRef(o)) {} };

// Staff context
struct Staff {
 // Staff sheet context
 Clef clef {NoClef,0};
 Sign octaveStart {.octave=OctaveStop}; // Current octave shift (for each staff)
 float octaveStartX = 0;
 uint beatTime = 0; // Time after last commited chord in .16 quarters since last time signature change
 //uint64 time = 0; // Time after last commited chord in ticks
 // Staff measure context
 //float x = 0;// = margin; 	// Holds current pen position for each line
 bool pendingWhole = false;
 Chord chord; // Current chord (per staff)
 uint beamStart = 0;
 Chords beam; // Chords belonging to current beam (per staff) (also for correct single chord layout)
 Chords tuplet;
};

// Layouts systems
struct System : SheetContext {
 // Staff context
 buffer<Staff> staves;

 // Fonts
 FontData& musicFont = *getFont("Bravura"_);
 Font& smallFont = musicFont.font(6.f*halfLineInterval);
 Font& font = musicFont.font(8.f*halfLineInterval);
 FontData& textFont = *getFont("LinLibertine"_);

 // Glyph methods
 vec2 glyphSize(uint code, Font* font_=0/*font*/) { Font& font=font_?*font_:this->font; return font.metrics(font.index(code)).size; }
 float glyphAdvance(uint code, Font* font_=0/*font*/) { Font& font=font_?*font_:this->font; return font.metrics(font.index(code)).advance; }
 int noteCode(const Sign& sign) { assert_(sign.type==Sign::Note); return min<int>(SMuFL::NoteHead::Breve+int(sign.note.value-1), int(SMuFL::NoteHead::Black)); };
 float noteSize(const Sign& sign) {
  return font.metrics(font.index(noteCode(sign))).advance;
 };

 // Metrics
 // Enough space for accidented dichords
 const float space = /*2*glyphSize(SMuFL::Accidental::Sharp, &smallFont).x+*/glyphSize(SMuFL::NoteHead::Black).x+glyphSize(SMuFL::Flag::Above).x;
 const float spaceWidth; // Minimum space width on initial layout pass, then space width for measure justification on final layout pass

 // Page context
 const float pageWidth;
 size_t pageIndex, systemIndex;
 Graphics* previousSystem;

 // System context
 size_t measureCount = 0;
 size_t lastMeasureBarIndex = -1;
 float allocatedLineWidth = 0;
 uint spaceCount = 0;
 //uint additionnalSpaceCount = 0;
 bool pageBreak = false;

 array<Sign> pendingClefChange; // Pending clef change to layout right before measure bar
 array<Chord> tremolo;

 // Layout output
 Graphics system;
 map<uint, float>* measureBars;
 struct TieStart { uint staff; int step; vec2 position; };
 array<TieStart>* activeTies;
 map<uint, array<Sign>>* notes;
 struct Range { int bottom = 0, top = 0; };
 const buffer<Range> line;

 //map<uint, float> timeTrack; // Maps time to horizontal position
 // Each measure has uniform time <-> horizontal position mapping fixed by shortestNote
 uint firstMeasureTime = 0; // in .16 quarters
 float measureStartX = 0;
 uint64 measureStartTime = 0; // in ticks
 uint shortestInterval = 0; // in ticks
 float X(uint time) {
  assert_(shortestInterval && measureStartTime <= time, shortestInterval, measureStartTime, time);
  return measureStartX + (time-measureStartTime)/shortestInterval*spaceWidth;
 }
 size_t justifiedSpace = 0;

 // -- Layout helpers
 float glyph(vec2 origin, uint code, float opacity=1, float size=8, FontData* font=0);
 void ledger(Sign sign, float x, float ledgerHalfLength=0);
 float stemX(const Chord& chord, bool stemUp);
 void layoutNotes(uint staff);

 // Evaluates vertical bounds
 buffer<Range> evaluateStepRanges(ref<Sign> signs) const;
 // Evaluates shortest interval
 uint evaluateShortestInterval(ref<Sign> signs) const;

 System(SheetContext context, ref<Staff> staves, float pageWidth, size_t pageIndex, size_t systemIndex, Graphics* previousSystem, mref<Sign> signs,
        map<uint, float>* measureBars = 0, array<TieStart>* activeTies = 0, map<uint, array<Sign>>* notes=0, float spaceWidth=0, bool measureNumbers=false);
};

// -- Layout output

float System::glyph(vec2 origin, uint code, float opacity, float size, FontData* font) {
 assert_(opacity <= 1);
 if(!font) font=&musicFont;
 size *= halfLineInterval;
 uint index = font->font(size).index(code);
 system.glyphs.append(origin, size, *font, code, index, black, opacity);
 return font->font(size).metrics(index).advance;
}

void System::ledger(Sign sign, float x, float ledgerHalfLength) { // Ledger lines
 uint staff = sign.staff;
 if(!ledgerHalfLength) ledgerHalfLength = noteSize(sign)*2/3;
 int step = clefStep(sign);
 float opacity = (sign.note.tie == Note::NoTie || sign.note.tie == Note::TieStart) ? 1 : 1./2;
 for(int s=2; s<=step; s+=2) {
  float y = staffY(staff, s);
  system.lines.append(vec2(x+noteSize(sign)/2-ledgerHalfLength,y),vec2(x+noteSize(sign)/2+ledgerHalfLength,y), black, opacity, true); // Ledger
 }
 for(int s=-10; s>=step; s-=2) {
  float y = staffY(staff, s);
  system.lines.append(vec2(x+noteSize(sign)/2-ledgerHalfLength,y),vec2(x+noteSize(sign)/2+ledgerHalfLength,y), black, opacity, true); // Ledger
 }
}

struct Step { uint staff; int step; };

static bool isStemUp(Step min, Step max) {
 const int middle = -4;
 int staff = min.staff == max.staff ? min.staff: -1;
 int minStep = min.step, maxStep = max.step;
 int aboveDistance = maxStep-middle, belowDistance = middle-minStep;
 return staff == -1 ?: aboveDistance == belowDistance ? !staff  : aboveDistance < belowDistance;
}
static bool isStemUp(ref<Chord> chords) {
 uint minStaff = chords[0][0].staff, maxStaff = minStaff;
 int minStep = clefStep(chords[0][0]), maxStep = minStep;
 for(const Chord& chord: chords) {
  minStaff = min(minStaff, chord[0].staff);
  minStep = min(minStep, clefStep(chord[0]));
  maxStaff = max(maxStaff, chord.last().staff);
  maxStep = max(maxStep, clefStep(chord.last()));
 }
 return isStemUp({minStaff, minStep}, {maxStaff, maxStep});
}

float System::stemX(const Chord& chord, bool stemUp) {
 //log("stemX", measureStartX, chord[0].time-measureStartTime, measureStartX, chord[0].time, X(chord[0].time)-measureStartX, X(chord[0].time));
 //assert_(X(chord[0].time)-measureStartX < 200);
 return X(chord[0].time) /*+ spaceWidth*/ + (stemUp ? noteSize(chord.last())-1 : 0);
}

bool anyAccidental(const Chord& chord){ for(const Sign& a: chord) if(a.note.accidental) return true; return false; }
bool allTied(const Chord& chord) {
 for(const Sign& a: chord) if(a.note.tie == Note::NoTie || a.note.tie == Note::TieStart) return false; return true;
}

void System::layoutNotes(uint staff) {
 auto& beam = staves[staff].beam;
 if(!beam) return;

 // Stems
 bool stemUp = isStemUp(beam);

 if(beam.size==1) { // Draws single stem
  assert_(beam[0]);
  Sign sign = stemUp ? beam[0].last() : beam[0].first();
  float yBottom = -inff, yTop = inff;
  for(Sign sign: beam[0]) if(sign.note.value >= Half) { yBottom = max(yBottom, Y(sign)); yTop = min(yTop, Y(sign)); } // inverted Y
  float yBase = stemUp ? yBottom-1./2 : yTop+1./2;
  float yStem = stemUp ? yTop-stemLength : yBottom+stemLength;
  float x = stemX(beam[0], stemUp);
  float opacity = allTied(beam[0]) ? 1./2 : 1;
  if(sign.note.value>=Half)
   system.lines.append(vec2(x, ::min(yBase, yStem)), vec2(x, max(yBase, yStem)), black, opacity, true); // Stem
  if(sign.note.value>=Eighth)
   glyph(vec2(x, yStem), (int(sign.note.value)-Eighth)*2 + (stemUp ? SMuFL::Flag::Above : SMuFL::Flag::Below), opacity, 7);
 } else if(beam.size==2) { // Draws pairing beam
  float x[2], base[2], tip[2];
  for(uint i: range(2)) {
   const Chord& chord = beam[i];
   x[i] = stemX(chord, stemUp);
   base[i] = Y(stemUp?chord.first():chord.last()) + (stemUp ? -1./2 : +1./2);
   tip[i] = Y(stemUp?chord.last():chord.first())+(stemUp?-1:1)*stemLength;
  }
  float midTip = (tip[0]+tip[1])/2; //farTip = stemUp ? min(tip[0],tip[1]) : max(tip[0],tip[1]);
  float delta[2] = {clamp(-lineInterval, tip[0]-midTip, lineInterval), clamp(-lineInterval, tip[1]-midTip, lineInterval)};
  Sign sign[2] = { stemUp?beam[0].last():beam[0].first(), stemUp?beam[1].last():beam[1].first()};
  for(uint i: range(2)) {
   float opacity = allTied(beam[i]) ? 1./2 : 1;
   tip[i] = midTip+delta[i];
   system.lines.append(vec2(x[i], ::min(base[i],tip[i])), vec2(x[i], ::max(base[i],tip[i])), black, opacity, true); // Stem
  }
  float opacity = allTied(beam[0]) && allTied(beam[1]) ? 1./2 : 1;
  Value first = max(apply(beam[0], [](Sign sign){return sign.note.value;}));
  Value second = max(apply(beam[1], [](Sign sign){return sign.note.value;}));
  // Beams
  for(size_t index: range(min(first,second)-Quarter)) {
   float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1) - !stemUp * beamWidth;
   vec2 p0 (x[0]-stemWidth/2, tip[0] + Y);
   vec2 p1 (x[1]+stemWidth/2, tip[1] + Y);
   system.parallelograms.append(p0, p1, beamWidth, black, opacity);
  }
  for(size_t index: range(min(first,second)-Quarter, first-Quarter)) {
   float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1) - !stemUp * beamWidth;
   vec2 p0 (x[0]-stemWidth/2, tip[0] + Y);
   vec2 p1 (x[1]+stemWidth/2, (tip[0]+tip[1])/2 + Y);
   p1 = (float(sign[1].duration)*p0 + float(sign[0].duration)*p1)/float(sign[0].duration+sign[1].duration);
   system.parallelograms.append(p0, p1, beamWidth, black, opacity);
  }
  for(size_t index: range(int(min(first,second)-Quarter), int(second-Quarter))) {
   float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1) - !stemUp * beamWidth;
   vec2 p0 (x[0]-stemWidth/2, tip[0] + Y);
   vec2 p1 (x[1]+stemWidth/2, tip[1] + Y);
   p0 = (float(sign[1].duration)*p0 + float(sign[0].duration)*p1)/float(sign[0].duration+sign[1].duration);
   system.parallelograms.append(p0, p1, beamWidth, black, opacity);
  }
 }
 else // Draws grouping beam
 {
  float firstStemY = Y(stemUp?beam.first().last():beam.first().first())+(stemUp?-1:1)*stemLength;
  float lastStemY = Y(stemUp?beam.last().last():beam.last().first())+(stemUp?-1:1)*stemLength;
  for(const Chord& chord: beam) {
   firstStemY = stemUp ? min(firstStemY, Y(chord.last())-shortStemLength) : max(firstStemY, Y(chord.first())+shortStemLength);
   lastStemY = stemUp ? min(lastStemY, Y(chord.last())-shortStemLength) : max(lastStemY, Y(chord.first())+shortStemLength);
  }
  array<float> stemsY;
  for(size_t index: range(beam.size)) { // Stems
   const Chord& chord = beam[index];
   float x = stemX(chord, stemUp);
   Sign sign = stemUp ? chord.first() : chord.last();
   float y = Y(sign) + (stemUp ? -1./2 : +1./2);
   float opacity = allTied(chord) ? 1./2 : 1;
   float stemY = firstStemY + (lastStemY-firstStemY) * (x - stemX(beam[0], stemUp))
     / (stemX(beam.last(), stemUp) - stemX(beam[0], stemUp));
   stemsY.append(stemY);
   system.lines.append(vec2(x, ::min(y, stemY)), vec2(x, ::max(stemY, y)), black, opacity, true); // Stem
  }
  // Beam
  for(size_t chordIndex: range(beam.size-1)) {
   Value value = ::min(beam[chordIndex][0].note.value, beam[chordIndex+1][0].note.value);
   for(size_t index: range(value-Quarter)) {
    float dy = (stemUp ? 1 : -1) * float(index) * (beamWidth+1) - !stemUp * beamWidth;
    system.parallelograms.append(
       vec2(stemX(beam[chordIndex], stemUp)-(chordIndex==0?1./2:0), stemsY[chordIndex]+dy),
       vec2(stemX(beam[chordIndex+1], stemUp)+(chordIndex==beam.size-1?1./2:0), stemsY[chordIndex+1]+dy), beamWidth);
   }
  }
 }

 // -- Chord layout
 float baseY = stemUp ? staffY(staff, 8) : staffY(staff, 0);
 for(Chord& chord: beam) {
  for(Sign& sign: chord) {
   Note& note = sign.note;
   // Adds courtesy accidental to implicitly altered note in chords with any accidentals
   if(!note.accidental && note.alteration && anyAccidental(chord)) {
    // TODO: Split such transformations (and the ones on MusicXML import) in a separate module
    note.accidental = alterationAccidental(note.alteration);
    note.accidentalOpacity = 1./4;
   }
  }

  // -- Dichord shifts
  buffer<bool> shift (chord.size); shift.clear();
  // Alternates starting from stem base
  if(stemUp) {
   int previousStep = chord[0].note.step;
   for(size_t index: range(1, chord.size)) {
    const Sign& sign = chord[index];
    const Note& note = sign.note;
    if(abs(note.step-previousStep)<=1) shift[index] = !shift[index-1];
    previousStep = note.step;
   }
  } else {
   int previousStep = chord.last().note.step;
   for(size_t index: reverse_range(chord.size-1)) {
    const Sign& sign = chord[index];
    const Note& note = sign.note;
    if(abs(note.step-previousStep)<=1) shift[index] = !shift[index+1];
    previousStep = note.step;
   }
  }

  // -- Accidental shifts
  buffer<int> accidentalShift (chord.size); accidentalShift.clear();
  // Shifts accidental left when note is left from stem
  if(1) for(size_t index: range(chord.size)) {
   const Sign& sign = chord[index];
   const Note& note = sign.note;
   if(note.accidental && (/*(stemUp && !shift[index]) ||*/ (!stemUp && shift[index])) ) accidentalShift[index] = 2;
  }
  // Alternates accidentals shifts
  size_t previousAccidentalIndex = 0;
  while(previousAccidentalIndex < chord.size && !chord[previousAccidentalIndex].note.accidental) previousAccidentalIndex++;
  if(previousAccidentalIndex < chord.size) {
   int previousAccidentalStep = chord[previousAccidentalIndex].note.step;
   for(size_t index: range(previousAccidentalIndex+1, chord.size)) {
    const Sign& sign = chord[index];
    const Note& note = sign.note;
    if(note.accidental) {
     if(note.step<=previousAccidentalStep+2) accidentalShift[index] = !accidentalShift[previousAccidentalIndex];
     previousAccidentalStep = note.step;
     previousAccidentalIndex = index;
    }
   }
  }
  // Shifts some more when a nearby note is dichord shifted
  if(1) for(size_t index: range(chord.size)) {
   const Sign& sign = chord[index];
   const Note& note = sign.note;
   if(note.accidental) {
    if( //((stemUp && !shift[index]) || (!stemUp && shift[index])) ||
        (index > 0            && abs(chord[index-1].note.step-note.step)<=1 && ((stemUp && !shift[index-1]) || (!stemUp && shift[index-1]))) ||
        (index < chord.size-1 && abs(chord[index+1].note.step-note.step)<=1 && ((stemUp && !shift[index+1]) || (!stemUp && shift[index+1])) )) {
     accidentalShift[index] += 1;
     //accidentalShift[index] += 2;
     //accidentalShift[index] = min(4, accidentalShift[index]+2);
    }
   }
  }

  for(size_t index: range(chord.size)) {
   Sign& sign = chord[index];
   assert_(sign.type==Sign::Note, int(sign.type));
   Note& note = sign.note;
   float x = X(sign.time), y = Y(sign);
   float opacity = (note.tie == Note::NoTie || note.tie == Note::TieStart) ? 1 : 1./2;

   if(tablature) {
    // Body
    {note.glyphIndex[2] = system.glyphs.size; // Records glyph index of body, i.e next glyph to be appended to system.glyphs :
     text(vec2(x, tablatureY(sign)), bold(str(note.fret)), note.grace?lineInterval:2*lineInterval, system.glyphs, vec2(1./2));
     if(system.glyphs.size-1 != note.glyphIndex[2]) note.glyphIndex[3] = system.glyphs.size-1; // 2nd glyph index (complex glyph composed of two digits)
    }
   }
   if(shift[index]) x += (stemUp?1:-1)*noteSize(sign);
   {
    // Ledger
    ledger(sign, x);
    // Body
    {note.glyphIndex[0] = system.glyphs.size; // Records glyph index of body, i.e next glyph to be appended to system.glyphs :
     glyph(vec2(x, y), noteCode(sign), opacity, note.grace?6:8); }
    // Dot
    if(note.dot) {
     float dotOffset = glyphSize(SMuFL::NoteHead::Black).x*7/6;
     glyph(vec2(X(sign.time) + (shift.contains(true) ? noteSize(sign) : 0)+dotOffset, Y(sign.staff, note.clef, note.step/2*2 +1)), SMuFL::Dot, opacity);
    }

    if(0) error(
       glyphAdvance(SMuFL::Accidental::Flat      , &smallFont), glyphSize(SMuFL::Accidental::Flat     , &smallFont).x,
       glyphAdvance(SMuFL::Accidental::Natural, &smallFont), glyphSize(SMuFL::Accidental::Natural, &smallFont).x,
       glyphAdvance(SMuFL::Accidental::Sharp  , &smallFont), glyphSize(SMuFL::Accidental::Sharp   , &smallFont).x );
    // Accidental
    if(note.accidental) {
     {note.glyphIndex[1] = system.glyphs.size; // Records glyph index of accidental, i.e next glyph to be appended to system.glyphs :
      float dx = 0; // Tweaks accidental advance
      if(note.accidental==Accidental::Flat) dx = glyphAdvance(SMuFL::Accidental::Flat, &smallFont)*1./4;
      if(note.accidental==Accidental::Natural) dx = glyphAdvance(SMuFL::Accidental::Natural, &smallFont)*1./3;
      if(note.accidental==Accidental::Sharp) dx = glyphAdvance(SMuFL::Accidental::Sharp, &smallFont)*1./2;
      glyph(vec2(X(sign.time) - accidentalShift[index] * glyphAdvance(SMuFL::Accidental::Sharp, &smallFont)*5./4
                 - glyphAdvance(sign.note.accidental, &smallFont) - dx, y), note.accidental, note.accidentalOpacity, 6);
     }
    }
   }

   // Tie
   if(sign.note.tie == Note::TieContinue || sign.note.tie == Note::TieStop) {
    size_t tieStart = invalid;
    if(activeTies) { // Skips on first pass
     auto& activeTies = *this->activeTies;
     for(size_t index: range(activeTies.size)) {
      if(activeTies[index].staff==staff && activeTies[index].step == note.step) {
       //assert_(tieStart==invalid, sign, apply(activeTies[staff],[](TieStart o){return o.step;}));
       tieStart = index;
      }
     }
    }
    if(tieStart != invalid) {
     //assert_(tieStart != invalid, sign, apply(activeTies[staff],[](TieStart o){return o.step;}), pages.size, pageSystems.size);
     TieStart tie = activeTies->take(tieStart);
     //log(apply(activeTies[staff],[](TieStart o){return o.step;}));

     //assert_(tie.position.y == Y(sign));
     if(tie.position.y != Y(sign)) log("tie.position.y != Y(sign)", tie.position.y, Y(sign));
     float x = X(sign.time), y = Y(sign);
     int slurDown = (chord.size>1 && index==chord.size-1) ? -1 :(y > staffY(staff, -4) ? 1 : -1);
     vec2 p0 = vec2(tie.position.x + noteSize(sign)/2 + (note.dot?space/2:0), y + slurDown*halfLineInterval);
     float bodyOffset = shift[index] ? noteSize(sign) : 0; // Shift body for dichords
     vec2 p1 = vec2(x + bodyOffset + noteSize(sign)/2, y + slurDown*halfLineInterval);
     if(x > tie.position.x) {
      const float offset = min(halfLineInterval, (x-tie.position.x)/4), width = halfLineInterval/2;
      vec2 k0 (p0.x, p0.y + slurDown*offset);
      vec2 k0p (k0.x, k0.y + slurDown*width);
      vec2 k1 (p1.x, p1.y + slurDown*offset);
      vec2 k1p (k1.x, k1.y + slurDown*width);
      system.cubics.append(copyRef(ref<vec2>({p0,k0,k1,p1,k1p,k0p})), black, 1.f/2);
     } else { // Wrapped tie
      assert_(pageWidth);
      {// Tie start
       vec2 P1 (pageWidth-margin, p1.y);
       const float offset = halfLineInterval, width = halfLineInterval/2;
       vec2 k0 (p0.x, p0.y + slurDown*offset);
       vec2 k0p (k0.x, k0.y + slurDown*width);
       vec2 k1 (P1.x, P1.y + slurDown*offset);
       vec2 k1p (k1.x, k1.y + slurDown*width);
       if(previousSystem) previousSystem->cubics.append(copyRef(ref<vec2>({p0,k0,k1,P1,k1p,k0p})), black, 1.f/2);
      }
      {// Tie end
       vec2 P0 (margin, p0.y);
       const float offset = halfLineInterval, width = halfLineInterval/2;
       vec2 k0 (P0.x, P0.y + slurDown*offset);
       vec2 k0p (k0.x, k0.y + slurDown*width);
       vec2 k1 (p1.x, p1.y + slurDown*offset);
       vec2 k1p (k1.x, k1.y + slurDown*width);
       system.cubics.append(copyRef(ref<vec2>({P0,k0,k1,p1,k1p,k0p})), black, 1.f/2);
      }
     }
    } else sign.note.tie = Note::NoTie;
   }
   if(sign.note.tie == Note::TieStart || sign.note.tie == Note::TieContinue) {
    float bodyOffset = shift[index] ? noteSize(sign) : 0; // Shift body for dichords
    if(activeTies) { // Skips on first pass
     auto& activeTies = *this->activeTies;
     activeTies.append(staff, note.step, vec2(X(sign.time)+bodyOffset, Y(sign)));
     //log(apply(activeTies[staff],[](TieStart o){return o.step;}));
    }
   }

   // Highlight
   note.pageIndex = pageIndex;
   if(measureBars) note.measureIndex = measureBars->size()-1;
   if(note.tie == Note::NoTie || note.tie == Note::TieStart) {
    if(notes) {
     assert_(sign.note.measureIndex != invalid);
     assert_(sign.note.signIndex != invalid);
     notes->sorted(sign.time).append( sign );
     //log("note", notes->size(), sign.time, notes->sorted(sign.time));
    }
   } //else log("-");
  }

  // Articulations
  {
   Sign sign = stemUp ? chord.first() : chord.last();
   float x = stemX(chord, stemUp) + (stemUp ? -1 : 1) * noteSize(sign)/2;
   int step = clefStep(sign);
   float y = Y(sign) + (stemUp?1:-1) * (lineInterval+(step%2?0:halfLineInterval));

   const int base = SMuFL::Articulation::Base + (stemUp?1:0);
   // TODO: combined articulations glyphs
   if(chord.first().note.accent) {
    int code = base+SMuFL::Articulation::Accent*2; glyph(vec2(x-glyphSize(code).x/2,y), code); y += lineInterval;
   }
   if(chord.first().note.staccato) {
    int code = base+SMuFL::Articulation::Staccato*2; glyph(vec2(x-glyphSize(code).x/2,y), code); y += lineInterval;
   }
   if(chord.first().note.tenuto) {
    int code = base+SMuFL::Articulation::Tenuto*2; glyph(vec2(x-glyphSize(code).x/2,y), code); y += lineInterval;
   }
   if(chord.first().note.trill) log("trill");
   baseY = stemUp ? max(baseY, y) : min(baseY, y);
  }
  // Arpeggio
  if(chord.first().note.arpeggio) {
   log("arpeggio");
   Sign sign = stemUp ? chord.first() : chord.last();
   float x = stemX(chord, stemUp) + (stemUp ? -1 : 1) * noteSize(sign)/2 - noteSize(sign);
   float y0 = Y(chord.first()), y1 = Y(chord.last());
   for(float y=y0; y<y1; y+=glyphSize(SMuFL::Arpeggio).x) glyph(vec2(x, y), SMuFL::Arpeggio);
  }
 }

 // Fingering
 if(0) for(const Chord& chord: beam) {
  array<int> fingering;
  for(const Sign& sign: chord) if(sign.note.finger) fingering.append( sign.note.finger );
  if(fingering) {
   float x = X(chord[0].time), y = baseY;
   x += noteSize(chord[0])/2;
   y = ::min(y, staffY(staff, 6));
   for(int finger: fingering.reverse()) { // Top to bottom
    //Font& font = textFont.font(textSize/2);
    //uint code = str(finger)[0];
    //auto metrics = font.metrics(font.index(code));
    //glyph(vec2(x-metrics.bearing.x-metrics.size.x/2,y+metrics.bearing.y), code, 1, 3, &textFont);
    text(vec2(x,y),str(finger),2*lineInterval,system.glyphs,vec2(1./2));
    y += lineInterval;
   }
  }
 }
 beam.clear();
}

buffer<System::Range> System::evaluateStepRanges(ref<Sign> signs) const {
 buffer<Staff> staves = copy(this->staves); // Follows clef and octaveStart changes in local scope
 buffer<Range> stepRanges {staves.size};
 stepRanges.clear(); //FIXME: first = {7, 0}, last = {-8, -7};
 for(Sign sign : signs) {
  uint staff = sign.staff;
  //if(sign.type==Sign::Repeat) lineHasTopText=true;
  if(sign.type == Sign::Clef) staves[staff].clef = sign.clef;
  else if(sign.type == Sign::OctaveShift) {
   if(sign.octave == Down) staves[staff].clef.octave++;
   else if(sign.octave == Up) staves[staff].clef.octave--;
   else if(sign.octave == OctaveStop) {
    assert_(staves[staff].octaveStart.octave==Down || staves[staff].octaveStart.octave==Up, int(staves[staff].octaveStart.octave));
    if(staves[staff].octaveStart.octave == Down) staves[staff].clef.octave--;
    if(staves[staff].octaveStart.octave == Up) staves[staff].clef.octave++;
   }
   else error(int(sign.octave));
   staves[staff].octaveStart = sign;
  }
  if(sign.type == Sign::Note) {
   sign.note.clef = staves[staff].clef; // FIXME: postprocess MusicXML instead
   stepRanges[staff].bottom = min(stepRanges[staff].bottom, clefStep(sign));
   stepRanges[staff].top = max(stepRanges[staff].top, clefStep(sign));
  }
 }
 return stepRanges;
}

uint System::evaluateShortestInterval(ref<Sign> signs) const {
 uint shortestInterval = -1;
 for(Sign sign : signs) {
  if(sign.type == Sign::Note || sign.type == Sign::Rest) {
   shortestInterval = ::min(shortestInterval, uint(sign.duration));
  }
 }
 return shortestInterval;
}

// Layouts a system
System::System(SheetContext context, ref<Staff> _staves, float pageWidth, size_t pageIndex, size_t systemIndex, Graphics* previousSystem, mref<Sign> signs,
               map<uint, float>* measureBars, array<TieStart>* activeTies, map<uint, array<Sign>>* notes, float _spaceWidth, bool measureNumbers)
 : SheetContext(context), staves(copyRef(_staves)), spaceWidth(_spaceWidth?:space), pageWidth(pageWidth), pageIndex(pageIndex), systemIndex(systemIndex), previousSystem(previousSystem),
   measureBars(measureBars), activeTies(activeTies), notes(notes), line(evaluateStepRanges(signs)) {

 // System first measure bar
 {float x = margin;
  if(1) { // Grand staff
   system.lines.append(vec2(x, staffY(0,-8)), vec2(x, staffY(staves.size-1,0)), black, 1.f/2, true); // Bar
  } else {
   for(size_t staff : range(staves.size)) {
    vec2 min(x, staffY(staff,0)), max(x, staffY(staff,-8));
    if(x) system.lines.append(min, max, black, 1.f/2, true);
   }
  }
  if(measureBars) (*measureBars)[signs[0].time] = x;
  x += halfLineInterval;
  //additionnalSpaceCount++;
  //timeTrack.insert(signs[0].time, x);
  //for(Staff& staff: staves) staff.x = x;
 }

 // Evaluates vertical bounds until next measure bar (full clear)
 size_t nextMeasureIndex = 0;
 while(nextMeasureIndex < signs.size && signs[nextMeasureIndex].type != Sign::Measure) nextMeasureIndex++;
 buffer<Range> measure = evaluateStepRanges(signs.slice(0, nextMeasureIndex));
 shortestInterval = evaluateShortestInterval(signs.slice(0, nextMeasureIndex));
 array<uint> measureNoteKeys;
 float measureFirstNote;

 for(size_t signIndex: range(signs.size)) {
  Sign sign = signs[signIndex];
  //if(0 && sign.type == Sign::Rest && sign.rest.value == Whole) continue; // FIXME: Skips whole rest (spacing workaround)
  /*auto nextStaffTime = [&](uint staff, int time) {
                                assert_(staff < staffCount);
                                //assert_(sign.time >= staves[staff].time);
                                if(sign.time > staves[staff].time) {
                                        uint unmarkedRestTickDuration = time - staves[staff].time;
                                        int unmarkedRestDuration = unmarkedRestTickDuration * quarterDuration / ticksPerSecond;
                                        //log("Unmarked rest", staff, staves[staff].beatTime, unmarkedRestDuration, staves[staff].time, time, unmarkedRestTickDuration);
                                        staves[staff].time += unmarkedRestTickDuration;
                                        staves[staff].beatTime += unmarkedRestDuration;
                                        // TODO: update time track
                                        //layoutNotes(staff);
                                }
                        };*/

  // Clears any pending clef changes
  if((sign.type == Sign::Note ||sign.type == Sign::Rest) || sign.type == Sign::KeySignature) {
      float advance = 0;
      for(size_t index=0; index<pendingClefChange.size;) {
          // Skips if not needed yet to keep the opportunity to clear right before measure bar
          if(((sign.type == Sign::Note||sign.type == Sign::Rest||sign.type == Sign::Clef||sign.type == Sign::OctaveShift) && sign.staff == pendingClefChange[index].staff) // Same staff
                  || sign.type == Sign::TimeSignature || sign.type == Sign::KeySignature) { // Non staff signs requiring pending clefs to be cleared
              Sign signClef = pendingClefChange.take(index);
              Clef clef = signClef.clef;
              float y = staffY(signClef.staff, clef.clefSign==GClef ? -6 : -2);
              ClefSign clefSign = clef.clefSign;
              if(clef.octave==1) clefSign = ClefSign(clefSign+SMuFL::Clef::_8va);
              else if(clef.octave==-1) clefSign = ClefSign(clefSign+SMuFL::Clef::_8vb);
              else assert(clef.octave==0, clef, clef.octave);
#if 0
              float& x = staves[signClef.staff].x; // No need to advance up to time track synchronization point
              if(sign.type == Sign::Note||sign.type == Sign::Rest) {
                  //x = max(x, min((x + timeTrack.at(sign.time) - glyphSize(clefSign).x)/2 /*Center between last staff sign and next synchronization point*/,
                  //timeTrack.at(sign.time) - glyphSize(clefSign).x));
                  x = max(x, (x + timeTrack.at(sign.time) + spaceWidth /*- glyphSize(clefSign).x*/)/2); /*Center between last staff sign and next synchronization point*/
              }
              x += glyph(vec2(x, y), clefSign);
              timeTrack[sign.time] = max(timeTrack[sign.time], x); // Updates next synchronization point
#else
              assert_(sign.time >= measureStartTime);
              //log(measureStartX);
              advance = ::max(advance, glyph(vec2(X(sign.time), y), clefSign)+space/2);
#endif
          } else { index++; continue; }
      }
      measureStartX += advance;
  }

  if(sign.type == Sign::Note||sign.type == Sign::Rest||sign.type == Sign::Clef||sign.type == Sign::OctaveShift) { // Staff signs
   //assert_(!pendingClefChange);
   uint staff = sign.staff;
   //nextStaffTime(staff, sign.time);
   //if(!timeTrack.contains(sign.time))
   // timeTrack.insert(sign.time, timeTrack.values[min(timeTrack.keys.linearSearch(sign.time), timeTrack.keys.size-1)]);
   assert_(sign.time >= measureStartTime, measureStartTime, sign.time, int(sign.type));
   float x = X(sign.time); //timeTrack.at(sign.time); // + timeTrack.values.last()) / 2; // FIXME: proportionnal to time

   if(sign.type == Sign::Clef || sign.type == Sign::OctaveShift) {
    /****/ if(sign.type == Sign::Clef) {
     Clef clef = sign.clef;
     if(staves[staff].clef.clefSign != clef.clefSign || staves[staff].clef.octave != clef.octave) {
      assert_(staves[staff].clef.clefSign != clef.clefSign || staves[staff].clef.octave != clef.octave);
      pendingClefChange.append(sign);
     }
     staves[staff].clef = clef;
    } else if(sign.type == Sign::OctaveShift) {
     float y = staffY(staff, staff ? max(0,line[staff].top+7) : min(4, line[staff].bottom-7));
     /****/  if(sign.octave == Down) {
      text(vec2(x, y), "8"+superscript("va"), textSize, system.glyphs, vec2(0, 1./2));
      staves[staff].clef.octave++;
      //timeTrack.at(sign.time).octave = x;
     } else if(sign.octave == Up) {
      text(vec2(x, y), "8"+superscript("vb"), textSize, system.glyphs, vec2(0, 1./2));
      staves[staff].clef.octave--;
      //timeTrack.at(sign.time).octave = x;
     }
     else if(sign.octave == OctaveStop) {
      assert_(staves[staff].octaveStart.octave==Down || staves[staff].octaveStart.octave==Up, int(staves[staff].octaveStart.octave));
      if(staves[staff].octaveStart.octave == Down) staves[staff].clef.octave--;
      if(staves[staff].octaveStart.octave == Up) staves[staff].clef.octave++;
      //assert_(staves[staff].clef.octave == 0, staff, staves[staff].clef.octave, pages.size, pageSystems.size, int(sign.octave));
      //if(!timeTrack.contains(sign.time)) timeTrack.insert(sign.time, timeTrack.values[min(timeTrack.keys.linearSearch(sign.time), timeTrack.keys.size-1)]);
      //float x = staves[staff].octaveStart.time < timeTrack.keys.first() ? 0 /*TODO: wrap*/: X(staves[staff].octaveStart.time);
      float x = staves[staff].octaveStartX;
      float start = x + space * 2;
      float end = x;
      for(float x=start; x<=end-space; x+=space*2) system.lines.append(vec2(x, y), vec2(x+space, y));
     }
     else error(int(sign.octave));
     staves[staff].octaveStart = sign;
     staves[staff].octaveStartX = X(staves[staff].octaveStart.time); // FIXME: multiline
     //log(pages.size, pageSystems.size, sign, staves[staff].clef.octave);
    } else error(int(sign.type));
    //timeTrack.at(sign.time).staves[staff].x = x;
   } else { // Note|Rest
    //x += spaceWidth;
    if(sign.type == Sign::Note) {
     Note& note = sign.note;

     if(0) {
         StringFret fingering = nextNote(sign); // Update fingering context (has to be called in temporal note order)
         note.string = fingering.string;
         note.fret = fingering.fret;
         signs[signIndex].note.string = note.string;
         signs[signIndex].note.fret = note.fret;
     }

     note.clef = staves[staff].clef;
     //for(const auto& sign: staves[staff].chord) if(sign.note.key() == note.key()) goto continue2_;

     if(note.tremolo /*== Note::Start*/) { tremolo.append(Chord{{sign}}); } // FIXME: tremolo chord
     if(note.tremolo == Note::Stop) {
      assert(tremolo.size == 2, tremolo.size);
      log("tremolo");
      // Draws pairing tremolo (similar to pairing beam)
      bool stemUp = true;
      float x[2], base[2], tip[2];
      for(uint i: range(2)) {
       const Chord& chord = tremolo[i];
       x[i] = stemX(chord, stemUp);
       base[i] = Y(stemUp?chord.first():chord.last()) + (stemUp ? -1./2 : +1./2);
       tip[i] = Y(stemUp?chord.last():chord.first())+(stemUp?-1:1)*stemLength;
      }
      float midTip = (tip[0]+tip[1])/2; //farTip = stemUp ? min(tip[0],tip[1]) : max(tip[0],tip[1]);
      float delta[2] = {clamp(-lineInterval, tip[0]-midTip, lineInterval), clamp(-lineInterval, tip[1]-midTip, lineInterval)};
      for(uint i: range(2)) tip[i] = midTip+delta[i];
      for(size_t index: range(3 /*FIXME: parseInteger(tremolo.text())*/)) {
       float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
       vec2 p0 (x[0]-stemWidth/2, tip[0]-beamWidth/2 + Y);
       vec2 p1 (x[1]+stemWidth/2, tip[1]-beamWidth/2 + Y);
       system.parallelograms.append(p0, p1, beamWidth, black);
      }
      tremolo.clear();
     }

     assert_(note.tie != Note::Merged);
     if(!note.grace) {
      if(!measureNoteKeys) measureFirstNote = x;
      measureNoteKeys.addSorted( note.key() );

      x += glyphAdvance(SMuFL::NoteHead::Black);
      /*for(Sign sign: staves[staff].chord) // Dichord
       if(abs(sign.note.step-note.step) <= 1) { x += glyphAdvance(SMuFL::NoteHead::Black); break; }*/
      sign.note.signIndex = signIndex;
      assert_(sign.note.signIndex != invalid);
      staves[staff].chord.insertSorted(sign);
      //log(".");
     } else { // Grace note
      //error("Grace");
#if GRACE || 1
      float dx = glyphSize(SMuFL::NoteHead::Black, &smallFont).x;
      float gx = x - dx - glyphAdvance(SMuFL::Flag::Above, &smallFont), y = Y(sign);

      ledger(sign, gx, dx);
      // Body
      {note.glyphIndex[0] = system.glyphs.size;
       glyph(vec2(gx, y), SMuFL::NoteHead::Black, 1, 6);}
      //assert_(!note.accidental);
      if(note.accidental) log("TODO: accidented grace");
      // TODO: stem down
      // Stem
      float stemX = gx + dx; //-1./2;
      system.lines.append(vec2(stemX, y-shortStemLength), vec2(stemX, y), black, 1.f/2, true); // Grace stem //-1./2
      // Flag
      glyph(vec2(stemX, y-shortStemLength), SMuFL::Flag::Above, 1, 6);
      // Slash
      if(note.acciaccatura) {
       glyph(vec2(stemX, y) - glyphSize(SMuFL::SlashUp, &smallFont)/2.f, SMuFL::SlashUp, 1, 6); // FIXME: highlight as well
      }
#endif
      note.pageIndex = pageIndex;
      if(measureBars) note.measureIndex = measureBars->size()-1;
      if(notes) {
       assert_(sign.note.measureIndex != invalid);
       assert_(sign.note.signIndex == invalid);
       sign.note.signIndex = signIndex;
       notes->sorted(sign.time).append(sign);
       //log("grace", notes->size(), sign.time, notes->sorted(sign.time));
      }
     }
    }
    else if(sign.type == Sign::Rest) {
     /*if(sign.time != staves[staff].time && !sign.rest.dot) {
                        //log("Unexpected rest start time", sign.time, (int)sign.rest.value, sign.rest.dot, staves[staff].time, pageIndex);
                    } else*/ {
      layoutNotes(staff);
      if(sign.rest.value == Whole) {
       assert_(!sign.rest.dot && !staves[staff].pendingWhole);
       staves[staff].pendingWhole = true;
       x += glyphAdvance(SMuFL::NoteHead::Black); // FIXME
      }
      else {
       vec2 p = vec2(x, staffY(staff, -4));
       x += glyph(p, SMuFL::Rest::Longa+int(sign.rest.value), 1./2, 6);
       //assert_(sign.rest.value >= Value::Long && sign.rest.value <= Value::Eighth, int(sign.rest.value));
       assert_(sign.rest.value >= Value::Long && sign.rest.value <= Value::Sixteenth, int(sign.rest.value));
       //assert_(!sign.rest.dot, signIndex, int(sign.rest.value));
       // Dot
       if(sign.rest.dot) {
        float dotOffset = glyphSize(SMuFL::Dot, &smallFont).x;
        glyph(vec2(x+dotOffset, staffY(staff, -3)), SMuFL::Dot, 1./2, 6);
       }
      }
      uint beatDuration = quarterDuration * 4 / timeSignature.beatUnit;
      uint measureLength = timeSignature.beats * beatDuration;
      uint duration = sign.rest.duration();
      if(sign.rest.value == Whole) duration = measureLength - staves[staff].beatTime%measureLength;
      staves[staff].beatTime += duration;
      //staves[staff].time += duration * ticksPerSecond * 60 / beatsPerMinute / quarterDuration; //sign.duration;
      //log("r", staves[staff].beatTime);
     }
    }
    else error(int(sign.type));

    // Advances any "time+duration" synchronization point inserted previously inserted to account for signs occuring in between on other staves.
    /*for(size_t index: range(timeTrack.size())) {
     if(timeTrack.keys[index] > sign.time && timeTrack.keys[index] <= sign.time+sign.duration) {
      timeTrack.values[index] = x;
     }
    }*/

    /*if(timeTrack.contains(sign.time+sign.duration)) {
                                                timeTrack.at(sign.time).staves[staff].x = x; // Advances position on staff
                                                timeTrack.at(sign.time+sign.duration).staff = max(timeTrack.at(sign.time+sign.duration).staff, x); // Advances synchronization point
                                        else timeTrack.insert(sign.time+sign.duration, {{x,x},x,x,x,x,x});*/
    //staves[staff].x = x;
    //timeTrack[sign.time+sign.duration] = max(timeTrack[sign.time+sign.duration], x);
   }
  } else if(sign.type == Sign::Measure || sign.type==Sign::KeySignature || sign.type==Sign::TimeSignature || sign.type==Sign::Repeat) {
   // Clearing signs (across staves)
   //for(size_t staff : range(staffCount)) nextStaffTime(staff, sign.time);

   /*if(!timeTrack.contains(sign.time)) { // FIXME
                                        float x = timeTrack.values.last().maximum();
                                        timeTrack.insert(sign.time, {{x,x},x,x,x,x,x});
                                }*/
   //assert_(timeTrack.contains(sign.time), int(sign.type), sign.time, timeTrack.keys);
   //float x = timeTrack.values.last(); //timeTrack.at(sign.time);//.maximum();
   float x = X(sign.time);

   //uint beatDuration = quarterDuration * 4 / timeSignature.beatUnit;
   //for(Staff& staff: staves) if(staff.beatTime % beatDuration != 0) staff.beatTime = 0;

   if(sign.type==Sign::TimeSignature) {
    for(Staff& staff: staves) staff.beatTime = 0;

    timeSignature = sign.timeSignature;
    String beats = str(timeSignature.beats);
    String beatUnit = str(timeSignature.beatUnit);
    float w = glyphAdvance(SMuFL::TimeSignature::_0);
    float W = max(beats.size, beatUnit.size)*w;
    float startX = x;
    x = startX + (W-beats.size*w)/2; // Align center
    for(char digit: beats) {
     for(size_t staff: range(staves.size)) glyph(vec2(x, staffY(staff, -2)), SMuFL::TimeSignature::_0+digit-'0');
     x += glyphAdvance(SMuFL::TimeSignature::_0+digit-'0');
    }
    float maxX = x;
    x = startX + (W-beatUnit.size*w)/2; // Align center
    for(char digit: beatUnit) {
     for(size_t staff: range(staves.size)) glyph(vec2(x, staffY(staff, -6)), SMuFL::TimeSignature::_0+digit-'0');
     x += glyphAdvance(SMuFL::TimeSignature::_0+digit-'0');
    }
    maxX = max(maxX, x)+glyphAdvance(SMuFL::TimeSignature::_0); //startX+2*glyphAdvance(SMuFL::TimeSignature::_0);
    measureStartX += maxX - startX;
    //timeTrack.at(sign.time) = maxX;
    //for(Staff& staff: staves) staff.x = maxX;
   } else { // Clears all lines (including direction lines)
    if(sign.type == Sign::Measure) {
     // Clears any pending notes
     for(size_t staff: range(staves.size)) layoutNotes(staff);

     // Clears any pending whole
     for(size_t staff: range(staves.size)) {
      if(staves[staff].pendingWhole) if(measureBars) {
       vec2 p = vec2((measureBars->values.last()+x)/2, staffY(staff, -2));
       glyph(p, SMuFL::Rest::Whole, 1./2, 6);
      }
      staves[staff].pendingWhole = false;
     }

     { array<uint> keys = ::move(measureNoteKeys);
      array<char> chord;
      if(measureNumbers && measureBars) chord.append(str(measureBars->size())+" "_); // Measure index
      if(keys && 0) { // Chord names
       uint root = keys[0];
       chord.append( strKey(keySignature, root) );
       if(keys.size>1) {
        uint third = keys[1];
        if(third-root == 3) chord.append("m");
       }
       float x = measureFirstNote;
       text(vec2(x, staffY(staves.size-1, max(11, line.last().top))), chord, textSize, system.glyphs, vec2(1./2,0/*1*/));
      }
      //error(chord);
      //log_(chord+" ");
      //log(chord, keys);
     }

     // Clears any pending clef changes right before measure bar (FIXME: defer to new system on break)
     float dx = 0;
     for(Sign sign: pendingClefChange) {
      Clef clef = sign.clef;
      float y = staffY(sign.staff, clef.clefSign==GClef ? -6 : -2);
      ClefSign clefSign = clef.clefSign;
      if(clef.octave==1) clefSign = ClefSign(clefSign+SMuFL::Clef::_8va);
      else if(clef.octave==-1) clefSign = ClefSign(clefSign+SMuFL::Clef::_8vb);
      else assert_(clef.octave==0, clef, clef.octave);
      dx = max(dx, glyph(vec2(x, y), clefSign));
     }
     pendingClefChange.clear();

     // Draws measure bar
     //if(measureBars) log("|", sign.time, x);
     x += dx;
     //x -= spaceWidth/2;
     if(margin || signIndex < signs.size-1) {
      if(1) { // Grand staff
       //if(measureBars)  log("|", x);
       system.lines.append(vec2(x, staffY(staves.size-1,0)), vec2(x, staffY(0,-8)), black, 1.f/2, true); // Bar
      } else {
       for(size_t staff : range(staves.size)) system.lines.append(vec2(x, staffY(staff,0)), vec2(x, staffY(staff,-8)), black, 1.f/2, true);
      }
     }
     if(measureBars) measureBars->insert(sign.time, x);

     // Line break
     //if(signIndex == signs.size-1) break; // End of line, last measure bar FIXME: skips last measure
     if(x > pageWidth && !measureBars && !activeTies && !notes && measureCount > 2 /*Never breaks with only 2 measures*/) {
      //log("end of line", pageWidth, x);
      break;
     }
     // Records current parameters at the end of this measure in case next measure triggers a line break
     measureCount++;
     //if(measureCount==5) break;
     if(sign.measure.lineBreak == Measure::PageBreak) pageBreak = true;
     lastMeasureBarIndex = signIndex;
     allocatedLineWidth = x + margin;
     //spaceCount = timeTrack.size() - 1; /*-1 as there is no space for last measure bar*/ // + additionnalSpaceCount;
     assert_(beatsPerMinute);
     uint currentMeasureDuration = staves[0].beatTime*ticksPerSecond *60/beatsPerMinute - measureStartTime;
     spaceCount = currentMeasureDuration / shortestInterval;

     //log("|", staves[0].beatTime);
     //if(measureCount==1)
     firstMeasureTime = staves[0].beatTime; // Track pickup duration to offset times for correct beaming

     // Evaluates next measure step ranges
     nextMeasureIndex++;
     while(nextMeasureIndex < signs.size && signs[nextMeasureIndex].type != Sign::Measure) nextMeasureIndex++;
     /*if(pageWidth && nextMeasureIndex < signs.size && signs[nextMeasureIndex].measure.lineBreak) {
                        log("Line break", pageWidth);
                        break;
                    }*/
     measure = evaluateStepRanges(signs.slice(signIndex, nextMeasureIndex-signIndex));
     shortestInterval = evaluateShortestInterval(signs.slice(0, nextMeasureIndex));
     //measureStartX = X(sign.time);//+spaceWidth;
     measureStartX = x+spaceWidth/2;
     measureStartTime = sign.time;
    }
    else if(sign.type==Sign::KeySignature) {
     float x0 = x;
     if(sign.keySignature == 0) {
      for(int i: range(abs(keySignature))) {
       int step = keySignature<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
       auto symbol = Accidental::Natural;
       glyph(vec2(x, Y(0, {staves[0].clef.clefSign, 0}, step - (staves[0].clef.clefSign==FClef ? 14 : 0))), symbol);
       x += glyph(vec2(x, Y(1, {staves[1].clef.clefSign, 0}, step - (staves[1].clef.clefSign==FClef ? 14 : 0))), symbol);
      }
     } else {
      for(int i: range(abs(sign.keySignature))) {
       int step = sign.keySignature<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
       auto symbol = sign.keySignature<0 ? Accidental::Flat : Accidental::Sharp;
       glyph(vec2(x, Y(0, {staves[0].clef.clefSign, 0}, step - (staves[0].clef.clefSign==FClef ? 14 : 0))), symbol);
       x += glyph(vec2(x, Y(1, {staves[1].clef.clefSign, 0}, step - (staves[1].clef.clefSign==FClef ? 14 : 0))), symbol);
      }
      x += space;
     }
     measureStartX += x - x0;
     keySignature = sign.keySignature;
    }
    else if(sign.type==Sign::Repeat) {
     if(measureBars) {
      if(int(sign.repeat)>0) { // Ending
       float x = measureBars->values.last();
       text(vec2(x, staffY(staves.size-1, max(0, line[1].top))), str(int(sign.repeat)), textSize, system.glyphs, vec2(0,1));
      } else {
       float dotX = (sign.repeat==Repeat::Begin ? measureBars->values.last()+space/2 : x-space/2)
         - glyphSize(SMuFL::Dot).x/2;
       for(size_t staff: range(staves.size)) {
        glyph(vec2(dotX, staffY(staff,-5)), SMuFL::Dot);
        glyph(vec2(dotX, staffY(staff,-3)), SMuFL::Dot);
       }
      }
     }
    }
    else error(int(sign.type));
    //x += space;
    //additionnalSpaceCount++; // Measure, Key, Time, Repeat adds additional spaces at a single ckck point
    //timeTrack.at(sign.time).setAll(x);
    // Sets all positions
    //for(Staff& staff: staves) staff.x = x;
    //timeTrack.at(sign.time) = x;
    //timeTrack[sign.time] = x;
   }
  } else if(sign.type == Sign::Tuplet) {
   const Tuplet tuplet = sign.tuplet;

   bool stemUp = isStemUp({signs[signIndex+tuplet.min].staff, clefStep(signs[signIndex+tuplet.min])}, {signs[signIndex+tuplet.max].staff, clefStep(signs[signIndex+tuplet.max])});
   bool above = stemUp;

   float x0 = X((signs[signIndex + (above ? tuplet.first.max : tuplet.first.min)]).time);// + spaceWidth;
   float x1 = X((signs[signIndex + (above ? tuplet. last.max : tuplet. last.min)]).time);// /* + spaceWidth*/ + glyphSize(SMuFL::NoteHead::Black).x;
   //float tx = Text(str(tupletSize.size), textSize, 0, 1, 0, "LinLibertine", false).sizeHint().x;
   float x = (x0+x1)/2;// + glyphSize(SMuFL::NoteHead::Black).x/2;// - tx;

   // FIXME: Stem length also depends on inner notes (TODO: factorize with beam layout code)
   //float y0 = Y(signs[signIndex + (above ? tuplet.max : tuplet.min)]) + (stemUp?-1:1)*stemLength;
   //float y1 = Y(signs[signIndex + (above ? tuplet.max : tuplet.min)]) + (stemUp?-1:1)*stemLength;
   float y0 = Y(signs[signIndex + (above ? tuplet.first.max : tuplet.first.min)]) + (stemUp?-1:1)*stemLength;
   float y1 = Y(signs[signIndex + (above ? tuplet.last.max : tuplet.last.min)]) + (stemUp?-1:1)*stemLength;
   //float y0 = Y(signs[signIndex + (above ? tuplet.first.max : tuplet.first.min)]) + (stemUp?-1:1)*shortStemLength;
   //float y1 = Y(signs[signIndex + (above ? tuplet.last.max : tuplet.last.min)]) + (stemUp?-1:1)*shortStemLength;
   /*float firstStemY = Y(stemUp?tuplet.first.max:tuplet.first.min)+(stemUp?-1:1)*stemLength;
   float lastStemY = Y(stemUp?tuplet.last.max:tuplet.first.max)+(stemUp?-1:1)*stemLength;
   for(const Chord& chord: beam) {
    firstStemY = stemUp ? min(firstStemY, Y(chord.last())-shortStemLength) : max(firstStemY, Y(chord.first())+shortStemLength);
    lastStemY = stemUp ? min(lastStemY, Y(chord.last())-shortStemLength) : max(lastStemY, Y(chord.first())+shortStemLength);
   }*/
   //float stemY = (firstStemY + lastStemY) / 2;
   float stemY = (y0 + y1) / 2;
   float dy = (above ? -1 : 1) * beamWidth;
   float y = stemY+dy;
   vec2 unused size = text(vec2(x,y), str(tuplet.size), textSize/2, system.glyphs, vec2(1./2, /*1./2*/above ? 1 : 0));
   /*if(uint(signs[signIndex+tuplet.last.min].time - signs[signIndex+tuplet.first.min].time) > ticksPerSecond*60/beatsPerMinute) { // No beam ? draw lines
    system.lines.append(vec2(x0, y0+dy), vec2(x-size.x, y0+((x-size.x)-x0)/(x1-x0)*(y1-y0)+dy), black);
    system.lines.append(vec2(x+size.x, y0+((x+size.x)-x0)/(x1-x0)*(y1-y0)+dy), vec2(x1, y1+dy), black);
   }*/
  }
  else { // Directions signs
   //if(!timeTrack.contains(sign.time)) timeTrack.insert(sign.time, timeTrack.values[min(timeTrack.keys.linearSearch(sign.time), timeTrack.keys.size-1)]);
   float x = X(sign.time);

   if(sign.type == Sign::Metronome) {
    if(beatsPerMinute!=sign.metronome.perMinute) {
     x += text(vec2(x, staffY(staves.size-1, 16)), "♩="_+str(sign.metronome.perMinute)+" "_, textSize, system.glyphs, vec2(0,0)).x;
     if(beatsPerMinute) log(beatsPerMinute, "->", sign.metronome.perMinute); // FIXME: variable tempo
     beatsPerMinute = sign.metronome.perMinute;
    }
   } else if(sign.type == Sign::ChordName) {
    //log(trim(ref<char>(sign.chordName.name)));
    x += text(vec2(x, staffY(staves.size-1, 16)), trim(sign.chordName.name), textSize, system.glyphs, vec2(0,0)).x;
   } else if(sign.type == Sign::ChordBox) {
    const int topFret = sign.chordBox.top;
    TextData s (sign.chordBox.name);
    array<int> notes;
    int step = "CDEFGAB"_.indexOf(s.next());
    assert_(step >= 0);
    int root = 72+ref<uint>{0,2,4,5,7,9,11}[step];
    if(s.match("♭")) root--;
    else if(s.match("♯")) root++;
    notes.append(root);
    notes.append(root+4); // Third
    notes.append(root+11); // Seventh
    notes.append(root+7); // Fifth
    log(root, strKey(0, root), strKey(0, root+4), strKey(0, root+11));
    float y = staffY(staves.size-1, -16);
    const int stringCount = 6, fretCount = 4;
    const float interval = lineInterval;
    for(int fret: range(fretCount+1)) system.fills.append(vec2(x+interval/2, y+fret*interval), vec2((stringCount-1)*interval, 1));
    for(int string: range(stringCount)) {
     const float x0 = x+string*interval;
     const float x1 = x+(string+1)*interval;
     //system.fills.append(vec2((x0+x1)/2, y), vec2(1, fretCount*interval));
     system.lines.append(vec2((x0+x1)/2, y), vec2((x0+x1)/2, y+fretCount*interval));
    }
    bool busy[stringCount] = {};
    for(int note: notes) {
     static int strings[] = {52, 57, 62, 67, 71, 76}; //EADGBe
     //assert_(strings[5]+fretCount >= strings[0]+12);
     if(note >= strings[5]+topFret+fretCount) note-=12;
     while(note < strings[0]+topFret) note+=12;
     StringFret sf = fingeringS(note, topFret, topFret+fretCount);
     int string = sf.string, fret = sf.fret-topFret;
     //assert_(fret <= fretCount, fret, note, string, fret);
     if(fret < 0) continue;//error("Low", sign.chordBox.top, sign.chordBox.name, string, fret, topFret+fret, strKey(0, note));
     if(busy[string]) {
      note-=12;
      StringFret sf = fingeringS(note, topFret, topFret+fretCount);
      string = sf.string, fret = sf.fret-topFret;
      if(busy[string]) {
       log("Busy", sign.chordBox.top, sign.chordBox.name, string, fret, topFret+fret, strKey(0, note));
       continue;
      }
     }
     //if(fret < 0) error("Low", sign.chordBox.top, sign.chordBox.name, string, fret, topFret+fret, strKey(0, note));
     if(fret >= 0 && fret < fretCount) {
      const float x0 = x+string*interval;
      const float x1 = x+(string+1)*interval;
      const float y1 = y+(fret+1)*interval;
      log(sign.chordBox.top, sign.chordBox.name, string, fret, topFret+fret, strKey(0, note));
      glyph(vec2(x0, y1),SMuFL::FilledCircle,1,7*interval/halfLineInterval);
      text(vec2((x0+x1)/2, y+fretCount*interval), strKey(0, note), interval, system.glyphs, vec2(1./2,0));
      busy[string] = true;
     }
    }
    text(vec2(x+5*halfLineInterval, y), trim(sign.chordBox.name), 3./2*interval, system.glyphs, vec2(1./2,1));
    text(vec2(x, y), bold(str(topFret)), interval, system.glyphs, vec2(1,0));
   } else if(sign.type == Sign::Dynamic) {
    assert_(sign.dynamic!=-1);
    //float y = (max(staffY(1, measure[1].bottom-2), staffY(0, measure[0].top/*+5*/)) + staffY(1, measure[1].bottom-2))/2;
    float y = (min(staffY(0, 0), staffY(0, measure[0].top)) + max(staffY(1, -8), staffY(1, measure[1].bottom-2)))/2;
    x += glyph(vec2(x+spaceWidth, y), sign.dynamic); // FIXME: staff assignment
   } else if(sign.type == Sign::Wedge) {
    float y = (staffY(1, -8)+staffY(0, 0))/2; // FIXME: staff assignment
    if(sign.wedge == WedgeStop) {
     bool crescendo = wedgeStart.wedge == Crescendo;
     //float x = wedgeStart.time < timeTrack.keys.first() ? 0 /*TODO: wrap*/: X(wedgeStart.time);
     float x = wedgeStartX;
     system.parallelograms.append( vec2(x, y+(-!crescendo-1)*3), vec2(x, y+(-crescendo-1)*3), 1.f);
     system.parallelograms.append( vec2(x, y+(!crescendo-1)*3), vec2(x, y+(crescendo-1)*3), 1.f);
    } else {
     wedgeStart = sign;
     wedgeStartX = X(wedgeStart.time);
    }
   } else if(sign.type == Sign::Pedal) {
    float y = staffY(0, min(-8,line[0].bottom)) + lineInterval; // FIXME: staff assignment
    if(sign.pedal == Start) {
     pedalStart = vec2(x, y);
     pedalStartSystemIndex = systemIndex;
     system.lines.append(vec2(x-space/2, y), vec2(x, y+lineInterval));
    }
    if(sign.pedal == Change || sign.pedal == PedalStop) {
     if(pedalStart) {
      assert_(pedalStart);
      if(pedalStartSystemIndex != systemIndex) {
       system.lines.append(vec2(margin, y+lineInterval), vec2(x, y+lineInterval));
      } else if(x > pedalStart.x) {
       assert(pedalStart.y == y);
       system.lines.append(pedalStart+vec2(0, lineInterval), vec2(x, y+lineInterval));
      }
     }
     if(sign.pedal == PedalStop) {
      system.lines.append(vec2(x, y), vec2(x, y+lineInterval));
      pedalStart = 0;
      pedalStartSystemIndex = 0;
     } else {
      system.lines.append(vec2(x, y+lineInterval), vec2(x+space/2, y));
      system.lines.append(vec2(x+space/2, y), vec2(x+space, y+lineInterval));
      pedalStart = vec2(x+space, y);
      pedalStartSystemIndex = systemIndex;
     }
    }
   } else error(int(sign.type));
  }

  //log("?");
  for(size_t staff: range(staves.size)) { // Layout notes, stems, beams and ties
   array<Chord>& beam = staves[staff].beam;
   Chord& chord = staves[staff].chord;
   if(chord && (signIndex==signs.size-1 || signs[signIndex+1].time != chord.last().time)) { // Complete chord

    Value value = chord[0].note.value;
    uint chordDuration = chord[0].note.duration();
    for(Sign sign: chord) {
     assert_(sign.type==Sign::Note, int(sign.type));
     Note& note = sign.note;
     chordDuration = min(chordDuration, note.duration());
     value = max(value, note.value);
    }

    if(beam) {
     uint beamDuration = 0;
     for(Chord& chord: beam) {
      assert_(chord);
      uint duration = chord[0].note.duration();
      for(Sign& sign: chord) {
       assert(sign.type == Sign::Note);
       duration = min(duration, sign.note.duration());
      }
      beamDuration += duration;
     }

     uint maximumBeamDuration = 2*quarterDuration;
     assert_(timeSignature.beatUnit == 2 || timeSignature.beatUnit == 4 || timeSignature.beatUnit == 8, timeSignature.beatUnit);
     //uint beatDuration = quarterDuration * 4 / timeSignature.beatUnit;
     //if(timeSignature.beats == 4 && timeSignature.beatUnit == 4) beatDuration *= 2; // FIXME: not always
     //if(/*timeSignature.beats == 6 &&*/ timeSignature.beatUnit == 8) beatDuration *= 2; // Beams quaver pairs even in /8 (i.e <=> 3/4)
     //if(timeSignature.beats == 6 && timeSignature.beatUnit == 8)
     //uint beatDuration = quarterDuration * 3 / 2; //timeSignature.beats / timeSignature.beatUnit;
     uint beatDuration = quarterDuration * 2;

     //log(firstMeasureTime, staves[staff].beatTime, (staves[staff].beatTime-firstMeasureTime)%beatDuration, beatDuration, chordDuration, beamDuration);
     if(beam[0][0].note.durationCoefficientDen>1) { // Tuplet
      if(beam.size == beam[0][0].note.durationCoefficientDen)
       layoutNotes(staff); // Full tuplet beams
     }
     else // Do no break tuplet beams
      if( beamDuration+chordDuration > maximumBeamDuration /*Beam before too long*/
        || beam.size >= 8 /*Beam before too many*/
        || (staves[staff].beatTime-firstMeasureTime)%beatDuration==0 /*Beam before spanning*/
        || beam[0][0].note.durationCoefficientDen != chord[0].note.durationCoefficientDen // Do not mix tuplet beams
        ) {
       layoutNotes(staff);
      }
    }

    // Beam (higher values are single chord "beams")
    if(!beam) staves[staff].beamStart = staves[staff].beatTime;
    if(value <= Quarter) layoutNotes(staff); // Flushes any pending beams
    //if(beam) assert_(beam[0][0].note.durationCoefficientDen == chord[0].note.durationCoefficientDen);
    beam.append(copy(chord));
    if(value <= Quarter) layoutNotes(staff); // Only stems for quarter and halfs (actually no beams :/)

    // Next
    chord.clear();
    //if(sign.time >= staves[staff].time) {
     //assert_(sign.time == staves[staff].time, sign.time, staves[staff].time);
     //staves[staff].time += chordDuration * ticksPerSecond*60/beatsPerMinute / quarterDuration;
     staves[staff].beatTime += chordDuration;
     //log("o", staves[staff].beatTime);
     //assert_(sign.time >= staves[staff].time, sign.time, staves[staff].time, chordDuration, ticksPerSecond*60/beatsPerMinute, quarterDuration);
    //} else error(sign.time, staves[staff].time, chordDuration, ticksPerSecond*60/beatsPerMinute, quarterDuration);
   }
  }
//continue2_:;
 }
 if(!pageWidth) pageWidth = measureBars->values.last();
 if(pedalStart) { // Draw pressed pedal line until end of line
  system.lines.append(pedalStart+vec2(0, lineInterval), vec2(pageWidth - margin, pedalStart.y + lineInterval));
 }
 // System raster
 if(measureBars) for(int staff: range(staves.size)) {
  for(int line: range(5)) {
   float y = staffY(staff, -line*2);
   assert_(measureBars);
   system.lines.append(vec2(measureBars->values[0], y), vec2(measureBars->values.last(), y), black, 3.f/4, true); // Raster
  }
  if(tablature) for(int line: range(6)) {
   float y = interstaffDistance + staffY(staff, -line*2);
   assert_(measureBars);
   system.lines.append(vec2(measureBars->values[0], y), vec2(measureBars->values.last(), y), black, 3.f/4, true); // Raster
  }
 }
 const int highMargin = 14, lowMargin = -18;
 system.bounds.min = vec2(0, staffY(staves.size-1, max(highMargin, line[staves.size-1].top)+2));
 system.bounds.max = vec2(pageWidth, staffY(0, min(lowMargin, line[0].bottom)));
 if(tablature) system.bounds.max.y = staffY(-1, -14);
}

inline bool operator ==(const Sign& sign, const uint& key) {
 assert_(sign.type == Sign::Note);
 return sign.note.key() == key;
}

// Layouts notations to graphic primitives (and parses notes to MIDI keys)
Sheet::Sheet(mref<Sign> signs, uint ticksPerSecond, int2 pageSize, float halfLineInterval, ref<MidiNote> midiNotes, string title, bool pageNumbers, bool measureNumbers)
 : pageSize(pageSize) {
 SheetContext context (ticksPerSecond, halfLineInterval);
 uint staffCount = 0; for(const Sign& sign: signs) if(sign.type == Sign::Note) staffCount = max(staffCount, sign.staff+1);
 buffer<Staff> staves {staffCount}; staves.clear(); //for(Staff& staff: staves) staff.x = context.margin;
 array<System::TieStart> activeTies;
 map<uint, array<Sign>> notes;

 /// Distributes systems evenly within page
 auto doPage = [this, pageSize, title, context, pageNumbers](mref<Graphics> systems) {
  float totalHeight = sum(apply(systems, [](const Graphics& o) { return o.bounds.size().y; }));
  if(pageSize.y) {
   if(totalHeight < pageSize.y) { // Spreads systems with margins
    float extra = (pageSize.y - totalHeight) / (systems.size+1);
    float offset = extra - systems[0].bounds.min.y;
    for(Graphics& system: systems) {
     system.translate(vec2(0, offset));
     offset += system.bounds.size().y + extra;
    }
   }
   if(totalHeight > pageSize.y) { // Spreads systems without margins
    float extra = (pageSize.y - totalHeight) / (systems.size-1);
    float offset = 0;
    for(Graphics& system: systems) {
     system.translate(vec2(0, offset));
     offset += system.bounds.size().y + extra;
    }
   }
  } else {
   float offset = -systems[0].bounds.min.y;
   assert_(isNumber(offset));
   for(Graphics& system: systems) {
    system.translate(vec2(0, offset));
    offset += system.bounds.size().y ;
   }
  }
  pages.append();
  for(Graphics& system: systems) {
   system.translate(system.offset); // FIXME: -> append
   if(!pageSize) assert_(!pages.last().glyphs);
   pages.last().append(system); // Appends systems first to preserve references to glyph indices
  }
  if(pages.size==1) text(vec2(pageSize.x/2, 0), bold(title), context.textSize, pages.last().glyphs, vec2(1./2, 0));
  if(pageNumbers) { // Page index numbers at each corner
   float margin = context.margin;
   text(vec2(margin,margin), str(pages.size), context.textSize, pages.last().glyphs, vec2(0, 0));
   //text(vec2(pageSize.x-margin,margin), str(pages.size), context.textSize, pages.last().glyphs, vec2(1, 0));
   //text(vec2(margin,pageSize.y-margin/2), str(pages.size), context.textSize, pages.last().glyphs, vec2(0, 1));
   //text(vec2(pageSize.x-margin,pageSize.y-margin/2), str(pages.size), context.textSize, pages.last().glyphs, vec2(1, 1));
  }
 };

 /// Layouts systems and evaluates minimum page count
 array<Graphics> totalSystems;
 size_t pageCount = 0;
 {
  float requiredHeight = 0;
  array<Graphics> pageSystems;
  for(size_t startIndex = 0; startIndex < signs.size;) { // Lines
   size_t breakIndex = signs.size;
   float spaceWidth = 0;
   if(pageSize.x) { // Evaluates next line break
    System system(context, staves, pageSize.x, pages.size, pageSystems.size, pageSystems ? &pageSystems.last() : nullptr, signs.slice(startIndex), 0, 0, 0, 0, false);
    breakIndex = startIndex + system.lastMeasureBarIndex + 1;
    if(!(startIndex < breakIndex && system.spaceCount)) { log("Empty line"); break; }
    assert_(startIndex < breakIndex && system.spaceCount, startIndex, breakIndex, system.spaceCount);
    spaceWidth = system.space + float(pageSize.x - system.allocatedLineWidth) / float(system.spaceCount);

    // Breaks page as needed
    if(pageSize.y) {
     float height = system.system.bounds.size().y;
     if(requiredHeight + height + pageSystems.size*4*context.lineInterval > pageSize.y) {
      pageCount++;
      requiredHeight = 0;
      totalSystems.append(move(pageSystems));
      pageSystems.clear();
     }
    }
   }

   // -- Layouts justified system
   System system(context, staves, pageSize.x, pages.size, pageSystems.size, pageSystems ? &pageSystems.last() : nullptr, signs.slice(startIndex, breakIndex-startIndex),
                 &measureBars, &activeTies, &notes, spaceWidth, measureNumbers);
   context = system; staves = copy(system.staves); // Copies back context for next line

   requiredHeight += system.system.bounds.size().y;
   pageSystems.append(move(system.system));
   this->lowestStep = min(this->lowestStep, system.line[0].bottom);
   this->highestStep = max(this->highestStep, system.line.last().top);

   startIndex = breakIndex;
  }
  pageCount++;
  totalSystems.append(move(pageSystems));
 }

 /// Distributes systems evenly between pages
 size_t maxSystemPerPage = (totalSystems.size+pageCount-1) / pageCount;
 {
  float requiredHeight = 0;
  array<Graphics> pageSystems;
  while(totalSystems) {
   Graphics system = totalSystems.take(0);
   if(pageSize.y) {
    float height = system.bounds.size().y;
    if(pageSystems.size >= maxSystemPerPage || requiredHeight + height+ pageSystems.size*4*context.lineInterval > pageSize.y) {
     requiredHeight = 0;
     doPage(pageSystems);
     pageSystems.clear();
    }
   }
   requiredHeight += system.bounds.size().y;
   pageSystems.append(move(system));
  }
  doPage(pageSystems);
  //assert_(measureBars.count() == 33, measureBars.count());
 }

 // Associates MIDI notes with score notes
 // chordToNote: First MIDI note index of chord
 midiToSign = buffer<Sign>(midiNotes.size, 0);
 constexpr bool logErrors = false;

#if 1
 if(midiNotes) {
  //firstSynchronizationFailureChordIndex = 0;
  array<array<uint>> S;
  array<array<uint>> Si; // Maps sorted index to original
  for(ref<Sign> chord: notes.values) {
   array<uint> bin;
   array<uint> binI;
   for(Sign note: chord) bin.append(note.note.key());
   //S.append(move(bin));
   array<uint> binS = copyRef(bin);
   sort(binS);
   for(size_t key: binS) binI.append(bin.indexOf(key));
   //log(S[min(M.size, S.size-1)], bin, binS, binI);
   S.append(move(binS));
   Si.append(move(binI));
  }
  /// Bins MIDI notes (TODO: cluster)
  array<array<uint>> M;
  array<array<uint>> Mi; // Maps original index to sorted
  for(size_t index = 0; index < midiNotes.size;) {
   array<uint> bin;
   array<uint> binI;
   int64 time = midiNotes[index].time;
   while(index < midiNotes.size && int64(midiNotes[index].time) < time+2048) { // TODO: cluster size with most similar bin count/size
    bin.append(midiNotes[index].key);
    index++;
   }
   array<uint> binS = copyRef(bin);
   sort(binS);
   for(size_t key: bin) binI.append(binS.indexOf(key));
   //log(S[min(M.size, S.size-1)], bin, binS, binI);
   M.append(move(binS));
   Mi.append(move(binI));
  }
  //log(S.size, sum(apply(S,[](ref<uint> b){return b.size;})), apply(S.slice(0,80),[](ref<uint> b){return b.size;}));
  //log(M.size, sum(apply(M,[](ref<uint> b){return b.size;})), apply(M.slice(0,80),[](ref<uint> b){return b.size;}));
  //log(Mi.slice(0,80));

  /// Synchronizes MIDI and score using dynamic time warping
  size_t m = S.size, n = M.size;
  //log(m, n);
  if(m > n) {
   log("m > n", m, n);
   return;
  }
  assert_(m <= n, m, n);

  // Evaluates cumulative score matrix at each alignment point (i, j)
  struct Matrix {
   size_t m, n;
   buffer<float> elements;
   Matrix(size_t m, size_t n) : m(m), n(n), elements(m*n) { elements.clear(0); }
   float& operator()(size_t i, size_t j) { return elements[i*n+j]; }
  } D(m,n);
  // Reversed scan here to have the forward scan when walking back the best path
  for(size_t i: reverse_range(m)) for(size_t j: reverse_range(n)) { // Evaluates match (i,j)
   float d = 0;
   for(uint s: S[i]) for(uint m: M[j]) d += s==m;
   // Evaluates best cumulative score to an alignment point (i,j)
   D(i,j) = max(max(
                 j+1==n?0:D(i,j+1), // Ignores peak j
                 i+1==m?0:D(i+1, j) ), // Ignores chord i
                ((i+1==m||j+1==n)?0:D(i+1,j+1)) + d ); // Matches chord i with peak j
  };

  // Evaluates _strictly_ monotonous map by walking back the best path on the cumulative score matrix
  // Forward scan (chronologic)
  size_t i = 0, j = 0; // Score and MIDI bins indices
  while(i<m && j<n) {
   /**/ if(i+1<m && D(i,j) == D(i+1,j)) {
    i++;
   }
   else if(j+1<n && D(i,j) == D(i,j+1)) {
    for(size_t unused k: range(M[j].size)) midiToSign.append(Sign{});
    j++;
   } else {
    for(size_t k: range(M[j].size)) {
     //assert_(i < notes.values.size && k < notes.values[i].size, i, k, notes.values.size);
     Sign sign{};
     if(Mi[j][k]<notes.values[i].size) sign = notes.values[i][Si[i][Mi[j][k]]]; // Map original MIDI index to sorted to original note index
     assert_(sign.note.signIndex != invalid);
     midiToSign.append( sign );
     Note note = sign.note;
     if(note.pageIndex != invalid && note.glyphIndex[0] != invalid) {
      vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex[0]].origin;
      text(p+vec2(2), strKey(0, note.key())+(note.key()!=M[j][k]?' '+strKey(0, M[j][k]):""_), 12, debug->glyphs);
     }
    }
    //for(size_t unused k: range(S[i].size, M[j].size)) midiToSign.append(Sign{});
    i++; j++;
   }
  }
  for(;j<n;j++) for(size_t unused k: range(M[j].size)) midiToSign.append(Sign{});

  assert_(midiToSign.size == midiNotes.size, midiNotes.size, midiToSign.size);
#if 0
  while(chordToNote.size < notes.size()) { // While map is not complete (iterates over notes)
   if(!notes.values[chordToNote.size]) { // Current chord depleted (all notes have been matched)
    chordToNote.append( midiToSign.size ); // Maps next chord to next index
    if(chordToNote.size==notes.size()) { // Was last chord
     assert(midiToSign.size == midiNotes.size, midiToSign.size == midiNotes.size);
     break;
    }
   }

   // Next note
   assert_(chordToNote.size<notes.size());
   array<Sign>& chord = notes.values[chordToNote.size]; // Current chord
   assert_(chord);

   //while(midiNotes[midiToSign.size].velocity == 0) midiToSign.append(Sign{}); // Not used
   uint midiIndex = midiToSign.size;
   if(midiIndex == midiNotes.size) { log("midiIndex == midiNotes.size"); break; } // FIXME
   assert_(midiIndex < midiNotes.size, midiIndex, midiNotes.size);
   uint midiKey = midiNotes[midiIndex];// .key ; // Current key

   // Finds current key in current chord
   int match = chord.indexOf(midiKey);
   if(match < 0) { log(midiKey, chord); match = 0; }// Take first if mismatch
   if(match >= 0) { // Key found in current chord
    Sign sign = chord.take(match);
    Note note = sign.note;
    //assert_(note.key() == midiKey);
    midiToSign.append( sign );
    if(note.pageIndex != invalid && note.glyphIndex != invalid) {
     vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
     text(p+vec2(space, 2), str(/*1?chordToNote.size:note.key()*/midiKey), 12, debug->glyphs);
    }
    //log("match", midiKey);
   } else error(chord, strKey(0, midiKey));
  }
#endif
 }
#else
 array<uint> chordExtra;
 if(midiNotes) {
  while(chordToNote.size < notes.size()) { // While map is not complete (iterates over notes)

   if(0) {
    //if(extraErrors > 40 || wrongErrors > 9 || missingErrors > 13 || orderErrors > 8) {
    //if(extraErrors || wrongErrors || missingErrors || orderErrors) {
    log(extraErrors, wrongErrors, missingErrors, orderErrors);
    log(midiToSign.size, midiNotes.size);
    log("MID", midiNotes.slice(midiToSign.size,7));
    log("XML", notes.values[chordToNote.size]);
    break;
   }

   // Next chord, match extras
   if(!notes.values[chordToNote.size]) { // Current chord depleted (all notes have been matched)
    chordToNote.append( midiToSign.size ); // Maps next chord to next index
    if(chordToNote.size==notes.size()) { // Was last chord
     assert_(!chordExtra);
     assert(midiToSign.size == midiNotes.size, midiToSign.size == midiNotes.size);
     break;
    }
    array<Sign>& chord = notes.values[chordToNote.size]; // Current chord
    // Tries to match any previous extra to next notes
    chordExtra.filter([&](uint midiIndex) {
     uint midiKey = midiNotes[midiIndex];
     int match = chord.indexOf(midiKey);
     if(match < 0) return false; // Keeps
     Sign sign = chord.take(match); Note note = sign.note;
     assert_(note.key() == midiKey);
     midiToSign[midiIndex] = sign;
     if(logErrors) log("O",str(note.key()));
     if(note.pageIndex != invalid && note.glyphIndex != invalid) {
      vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
      text(p+vec2(space, 2), "O"_+str(note.key()), 12, debug->glyphs);
     }
     orderErrors++;
     log("order error");
     return true; // Discards
    });
    if(chordExtra) {
     if(logErrors) log("+"_+str(apply(chordExtra, [&](const uint index){return midiNotes[index];})));
     //if(!(chordExtra.size<=2)) { log("chordExtra.size<=2", chordExtra.size); break; }
     //assert_(chordExtra.size<=2, chordExtra.size);
     extraErrors+=chordExtra.size;
     chordExtra.clear();
    }
    if(!notes.values[chordToNote.size]) { // Current chord depleted (all notes have been matched with extras)
     chordToNote.append( midiToSign.size );
    }
    assert_(chordToNote.size<notes.size());
    if(chordToNote.size==notes.size()) {
     assert_(!chordExtra);
     assert(midiToSign.size == midiNotes.size, midiToSign.size == midiNotes.size);
     break;
    }
   }

   // Next note
   assert_(chordToNote.size<notes.size());
   array<Sign>& chord = notes.values[chordToNote.size]; // Current chord
   assert_(chord);

   uint midiIndex = midiToSign.size;
   if(midiIndex == midiNotes.size) { log("midiIndex == midiNotes.size"); break; } // FIXME
   assert_(midiIndex < midiNotes.size, midiIndex, midiNotes.size);
   uint midiKey = midiNotes[midiIndex]; // Current key

   // Finds current key in current chord
   int match = chord.indexOf(midiKey);
   if(match >= 0) { // Key found in current chord
    Sign sign = chord.take(match);
    Note note = sign.note;
    assert_(note.key() == midiKey);
    midiToSign.append( sign );
    if(note.pageIndex != invalid && note.glyphIndex != invalid) {
     vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
     text(p+vec2(space, 2), str(note.key()), 12, debug->glyphs);
    }
    //log("match", midiKey);
   }
   else { // Key not found
    // Tries to match current chord with previous extra MIDI notes (chord ordering error)
    //assert_(chordExtra.size <= chord.size);
    if(/*chordExtra &&*/ chord.size == chordExtra.size) {
     int match = notes.values[chordToNote.size+1].indexOf(midiNotes[chordExtra[0]]);
     if(match >= 0) { // First extra MIDI note match any notes from next chord
      //assert_(chord.size<=3/*, chord*/);
      if(chord.size>3) log(chord);
      if(logErrors) log("-"_+str(chord));
      missingErrors += chord.size;
      chord.clear(); // Assumes full chord has been played
      chordToNote.append( midiToSign.size ); // Next chord
      chordExtra.filter([&](uint index) {
       //*chord.clear() => if(!notes.values[chordToNote.size])*/ chordToNote.append( midiToSign.size );
       array<Sign>& chord = notes.values[chordToNote.size];
       //assert_(chord, chordToNote.size, notes.size());
       int match = chord.indexOf(midiNotes[index]);
       if(match<0) {
        return false; // Keeps extra notes which do not match with current chord
       }
       midiKey = midiNotes[index];
       Sign sign = chord.take(match); Note note = sign.note;
       assert_(midiKey == note.key());
       midiToSign[index] = sign; // Maps extra MIDI notes to next chord score notes
       if(note.pageIndex != invalid && note.glyphIndex != invalid) {
        vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
        text(p+vec2(space, 2), str(note.key()), 12, debug->glyphs);
       }
       return true; // Discards extra which have now just been matched to next chord
      });
     } else { // First extra MIDI notes did not match any notes from next chord
      assert_(midiKey != chord[0].note.key());
      uint previousSize = chord.size;
      // Removes any score notes which are not found within the next 5 MIDI notes
      chord.filter([&](const Sign& sign) {
       Note note = sign.note;
       if(midiNotes.slice(midiIndex,5).contains(note.key())) { // If found
        return false; // Keeps extra score note to be matched later
       }
       // If not found: Matches extra score notes to extra MIDI notes
       uint midiIndex = chordExtra.take(0);
       uint midiKey = midiNotes[midiIndex];
       assert_(note.key() != midiKey);
       if(logErrors) log("!"_+str(note.key(), (int)midiKey));//, note.measureIndex==invalid?"invalid"__:str(note.measureIndex));
       if(note.pageIndex != invalid && note.glyphIndex != invalid) {
        vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
        text(p+vec2(space, 2), str(note.key())+"?"_+str(midiKey)+"!"_, 12, debug->glyphs);
       }
       wrongErrors++;
       midiToSign[midiIndex] = sign; // Match highlight anyway (in case sync is wrong)
       return true; // Discards as wrong
      });
      if(previousSize == chord.size) { // No notes have been filtered out as wrong, remaining notes are extra MIDI notes
       assert_(chordExtra && chordExtra.size<=3, chordExtra.size);
       if(logErrors) log("+"_+str(apply(chordExtra, [&](const uint index){return midiNotes[index];})));
       extraErrors += chordExtra.size;
       chordExtra.clear();
      }
     }
    } else { // Not enough extra MIDI notes to match current chord
     // Records next MIDI note as an extra note to be matched later
     midiToSign.append({.note={}});
     chordExtra.append( midiIndex );
     if(logErrors) log("?"_,midiKey);
    }
   }
  }
 }
 log(midiToSign.size, midiNotes.size, chordToNote.size, notes.size());
 //assert_(!chordExtra);
 if(chordExtra) log(chordExtra);
#endif
 //assert_(midiToSign.size == midiNotes.size, midiToSign.size, midiNotes.size);
 assert_(midiToSign.size <= midiNotes.size, midiToSign.size, midiNotes.size);
 if(chordToNote.size == notes.size() || !midiNotes) {}
 //else { firstSynchronizationFailureChordIndex = chordToNote.size; } FIXME
 if(logErrors) { // && (extraErrors||wrongErrors||missingErrors||orderErrors)) {
  log(extraErrors, wrongErrors, missingErrors, orderErrors);
  pages[0].graphics.insertMulti(vec2(0), share(debug));
 }
 //pages[0].graphics.insertMulti(vec2(0), share(debug));
}
