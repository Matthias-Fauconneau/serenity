#include "sheet.h"
#include "notation.h"
#include "text.h"

static int clefStep(Clef clef, int step) { return step - (clef.clefSign==GClef ? 10 : -2) - clef.octave*7; } // Translates C4 step to top line step using clef
static int clefStep(Sign sign) { assert_(sign.type==Sign::Note); return clefStep(sign.note.clef, sign.note.step); }

static vec2 text(vec2 origin, string message, float fontSize, array<Glyph>& glyphs, vec2 align=0 /*0:left|top,1/2,center,1:right|bottom*/) {
    Text text(message, fontSize, 0, 1, 0, "LinLibertine");
    vec2 textSize = text.textSize();
    origin -= align*textSize;
    auto textGlyphs = move(text.graphics(0)->glyphs); // Holds a valid reference during iteration
    for(auto glyph: textGlyphs) { glyph.origin+=origin; glyphs.append(glyph); }
    return textSize;
}

// Sheet paraeters and musical context
struct SheetContext {
    // Sheet parameters
    static constexpr int staffCount = 2;
    float halfLineInterval, lineInterval = 2*halfLineInterval;
    float stemWidth = 0, stemLength = 7*halfLineInterval, beamWidth = halfLineInterval;
    float shortStemLength = 5*halfLineInterval;
    float margin = 1;
    float textSize = 6*halfLineInterval;

    // Musical context
    uint ticksPerMinutes = 0;
    Clef clefs[staffCount] = {{NoClef,0}, {NoClef,0}}; KeySignature keySignature = 0; TimeSignature timeSignature = {4,4};
    Sign octaveStart[staffCount] {{.octave=OctaveStop}, {.octave=OctaveStop}}; // Current octave shift (for each staff)
    vec2 pedalStart = 0; size_t pedalStartSystemIndex=0; // Last pedal start/change position
    Sign wedgeStart {.wedge={}}; // Current wedge

    uint ticksPerQuarter;
    uint beatTime[staffCount] = {0,0}; // Time after last commited chord in .16 quarters since last time signature change
    uint staffTime[staffCount] = {0,0}; // Time after last commited chord in ticks

    SheetContext(float halfLineInterval, uint ticksPerQuarter) : halfLineInterval(halfLineInterval), ticksPerQuarter(ticksPerQuarter) {}
};

// Layouts systems
struct System : SheetContext {
    // Fonts
    FontData& musicFont = *getFont("Bravura"_);
    Font& smallFont = musicFont.font(6.f*halfLineInterval);
    Font& font = musicFont.font(8.f*halfLineInterval);
    FontData& textFont = *getFont("LinLibertine"_);

    // Glyph methods
    vec2 glyphSize(uint code, Font* font_=0/*font*/) { Font& font=font_?*font_:this->font; return font.metrics(font.index(code)).size; }
    float glyphAdvance(uint code, Font* font_=0/*font*/) { Font& font=font_?*font_:this->font; return font.metrics(font.index(code)).advance; }
    int noteCode(const Sign& sign) { return min(SMuFL::NoteHead::Whole+int(sign.note.value), int(SMuFL::NoteHead::Black)); };
    float noteSize(const Sign& sign) { return font.metrics(font.index(noteCode(sign))).advance; };

    // Metrics
    const float space = glyphSize(SMuFL::Accidental::Flat, &smallFont).x+glyphSize(SMuFL::NoteHead::Black).x; // Enough space for accidented dichords
    const float spaceWidth; // Minimum space on pass 0, Space width for measure justification on pass 1

    // Page context
    const float pageWidth;
    size_t pageIndex, systemIndex;
    Graphics* previousSystem;

    // System context
    size_t lastMeasureBarIndex = -1;
    float allocatedLineWidth = 0;
    uint spaceCount = 0;
    uint additionnalSpaceCount = 0;
    //bool pageBreak = false;

    // Measure context
    bool pendingWhole[staffCount] = {false, false};
    typedef array<Sign> Chord; // Signs belonging to a same chord (same time)
    array<Chord> tuplets[staffCount];
    uint beamStart[staffCount] = {0,0};
    array<Chord> beams[staffCount]; // Chords belonging to current beam (per staff) (also for correct single chord layout)
    Chord chords[staffCount]; // Current chord (per staff)

    // Layout output
    Graphics system;
    map<uint, float>* measureBars;
    struct TieStart { uint staff; int step; vec2 position; };
    array<TieStart>* activeTies;
    map<uint, array<Sign>>* notes;
    struct Range { int bottom, top; };
    const buffer<Range> line;


    struct Position { // Holds current pen position for each line
        float staffs[staffCount];
        float middle; // Dynamic, Wedge
        float metronome; // Metronome
        float octave; // OctaveShift
        float bottom; // Pedal
        /// Maximum position
        float maximum() { return max(max(staffs), max(middle, max(max(metronome, octave), bottom))); }
        /// Synchronizes staff positions to \a x
        void setStaves(float x) { for(float& staffX: staffs) staffX = max(staffX, x); }
        /// Synchronizes all positions to \a x
        void setAll(float x) { setStaves(x); middle = x; metronome = x; octave = x; bottom = x; }
    };
    map<uint, Position> timeTrack; // Maps times to positions

    float& staffX(uint time, int type, uint staff);
    float& X /*[staffX]*/(const Sign& sign) { return staffX(sign.time, sign.type, sign.staff); };
    vec2 P(const Sign& sign) { return vec2(X(sign), Y(sign)); };

    // Vertical positioning
    float staffY(uint staff, int clefStep) { return (!staff)*(10*lineInterval+halfLineInterval) - clefStep * halfLineInterval; }
    float Y(uint staff, Clef clef, int step) { return staffY(staff, clefStep(clef, step)); };
    float Y(Sign sign) { assert_(sign.type==Sign::Note); return staffY(sign.staff, clefStep(sign)); };

    // -- Layout output
    float glyph(vec2 origin, uint code, float opacity=1, float size=8, FontData* font=0);
    void ledger(Sign sign, float x);
    void layoutNotes(uint staff);

    // Evaluates vertical bounds until next line break
    buffer<Range> evaluateStepRanges/*[clefs, octaveStart*/(ref<Sign> signs);

    System(SheetContext context, float pageWidth, size_t pageIndex, size_t systemIndex, Graphics* previousSystem, ref<Sign> signs,
           map<uint, float>* measureBars = 0, array<TieStart>* activeTies = 0, map<uint, array<Sign>>* notes=0, float spaceWidth=0);
};

float& System::staffX(uint time, int type, uint staff) {
    if(!timeTrack.contains(time)) {
        //log("!timeTrack.contains(time)", time, int(type));
        assert_(type==Sign::Clef || type==Sign::OctaveShift || type==Sign::Wedge || type==Sign::Pedal || type==Sign::Note/*FIXME*/, int(type));
        size_t index = timeTrack.keys.linearSearch(time);
        index = min(index, timeTrack.keys.size-1);
        assert_(index < timeTrack.keys.size);
        float x;
        if(type == Sign::Wedge) x = timeTrack.values[index].middle;
        else if(type == Sign::Metronome) x = timeTrack.values[index].metronome;
        else if(type == Sign::Pedal) x = timeTrack.values[index].bottom;
        else if(type == Sign::Note || type == Sign::Rest|| type == Sign::Clef|| type == Sign::OctaveShift) {
            assert_(staff < 2, int(type));
            x = timeTrack.values[index].staffs[staff];
        } else error(int(type));
        timeTrack.insert(time, {{x,x},x,x,x,x});
    }
    assert_(timeTrack.contains(time), time, timeTrack.keys);
    float* x = 0;
    if(type == Sign::Metronome) x = &timeTrack.at(time).metronome;
    else if(type == Sign::OctaveShift) x = &timeTrack.at(time).octave;
    else if(type==Sign::Dynamic || type==Sign::Wedge) x = &timeTrack.at(time).middle;
    else if(type == Sign::Pedal) x = &timeTrack.at(time).bottom;
    else if(type == Sign::Note || type == Sign::Rest|| type == Sign::Clef|| type == Sign::OctaveShift) {
        assert_(staff < 2, staff, int(type));
        x = &timeTrack.at(time).staffs[staff];
    } else error(int(type));
    return *x;
}

// -- Layout output

float System::glyph(vec2 origin, uint code, float opacity, float size, FontData* font) {
    if(!font) font=&musicFont;
    size *= halfLineInterval;
    uint index = font->font(size).index(code);
    system.glyphs.append(origin, size, *font, code, index, black, opacity);
    return font->font(size).metrics(index).advance;
}

void System::ledger(Sign sign, float x) { // Ledger lines
    uint staff = sign.staff;
    float ledgerLength = 2*noteSize(sign);
    int step = clefStep(sign);
    float opacity = (sign.note.tie == Note::NoTie || sign.note.tie == Note::TieStart) ? 1 : 1./2;
    for(int s=2; s<=step; s+=2) {
        float y = staffY(staff, s);
        system.lines.append(vec2(x+noteSize(sign)/2-ledgerLength/2,y),vec2(x+noteSize(sign)/2+ledgerLength/2,y), black, opacity);
    }
    for(int s=-10; s>=step; s-=2) {
        float y = staffY(staff, s);
        system.lines.append(vec2(x+noteSize(sign)/2-ledgerLength/2,y),vec2(x+noteSize(sign)/2+ledgerLength/2,y), black, opacity);
    }
};

void System::layoutNotes(uint staff) {
    auto& beam = beams[staff];
    if(!beam) return;

    // Stems
    int minStep = clefStep(beam[0][0]), maxStep = minStep;
    for(Chord& chord: beam) {
        minStep = min(minStep, clefStep(chord[0]));
        maxStep = max(maxStep, clefStep(chord.last()));
    }
    const int middle = -4;
    int aboveDistance = maxStep-middle, belowDistance = middle-minStep;
    bool stemUp = aboveDistance == belowDistance ? !staff  : aboveDistance < belowDistance;

    auto anyAccidental = [](const Chord& chord){
        for(const Sign& a: chord) if(a.note.accidental) return true;
        return false;
    };
    auto allTied = [](const Chord& chord) {
        for(const Sign& a: chord) if(a.note.tie == Note::NoTie || a.note.tie == Note::TieStart) return false;
        return true;
    };
    auto stemX = [&](const Chord& chord, bool stemUp) {
        return X(chord[0]) + (stemUp ? noteSize(chord[0])-1 : 0);
        };

        if(beam.size==1) { // Draws single stem
            assert_(beam[0]);
            Sign sign = stemUp ? beam[0].last() : beam[0].first();
            float yBottom = -inf, yTop = inf;
            for(Sign sign: beam[0]) if(sign.note.value >= Half) { yBottom = max(yBottom, Y(sign)); yTop = min(yTop, Y(sign)); } // inverted Y
            float yBase = stemUp ? yBottom-1./2 : yTop+1./2;
            float yStem = stemUp ? yTop-stemLength : yBottom+stemLength;
            float x = stemX(beam[0], stemUp);
            float opacity = allTied(beam[0]) ? 1./2 : 1;
            if(sign.note.value>=Half)
                system.lines.append(vec2(x, ::min(yBase, yStem)), vec2(x, max(yBase, yStem)), black, opacity);
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
            float delta[2] = {clip(-lineInterval, tip[0]-midTip, lineInterval), clip(-lineInterval, tip[1]-midTip, lineInterval)};
            Sign sign[2] = { stemUp?beam[0].last():beam[0].first(), stemUp?beam[1].last():beam[1].first()};
            for(uint i: range(2)) {
                float opacity = allTied(beam[i]) ? 1./2 : 1;
                tip[i] = midTip+delta[i];
                system.lines.append(vec2(x[i], ::min(base[i],tip[i])), vec2(x[i], ::max(base[i],tip[i])), black, opacity);
            }
            float opacity = allTied(beam[0]) && allTied(beam[1]) ? 1./2 : 1;
            Value first = max(apply(beam[0], [](Sign sign){return sign.note.value;}));
            Value second = max(apply(beam[1], [](Sign sign){return sign.note.value;}));
            // Beams
            for(size_t index: range(min(first,second)-Quarter)) {
                float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
                vec2 p0 (x[0]-stemWidth/2, tip[0]-beamWidth/2 + Y);
                vec2 p1 (x[1]+stemWidth/2, tip[1]-beamWidth/2 + Y);
                system.parallelograms.append(p0, p1, beamWidth, black, opacity);
            }
            for(size_t index: range(min(first,second)-Quarter, first-Quarter)) {
                float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
                vec2 p0 (x[0]-stemWidth/2, tip[0]-beamWidth/2 + Y);
                vec2 p1 (x[1]+stemWidth/2, (tip[0]+tip[1])/2-beamWidth/2 + Y);
                p1 = (float(sign[1].duration)*p0 + float(sign[0].duration)*p1)/float(sign[0].duration+sign[1].duration);
                system.parallelograms.append(p0, p1, beamWidth, black, opacity);
            }
            for(size_t index: range(int(min(first,second)-Quarter), int(second-Quarter))) {
                float Y = (stemUp ? 1 : -1) * float(index) * (beamWidth+1);
                vec2 p0 (x[0]-stemWidth/2, tip[0]-beamWidth/2 + Y);
                vec2 p1 (x[1]+stemWidth/2, tip[1]-beamWidth/2 + Y);
                p0 = (float(sign[1].duration)*p0 + float(sign[0].duration)*p1)/float(sign[0].duration+sign[1].duration);
                system.parallelograms.append(p0, p1, beamWidth, black, opacity);
            }
        } else { // Draws grouping beam
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
                system.lines.append(vec2(x, ::min(y, stemY)), vec2(x, ::max(stemY, y)), black, opacity);
            }
            // Beam
            for(size_t chordIndex: range(beam.size-1)) {
                const Chord& chord = beam[chordIndex];
                Value value = chord[0].note.value;
                for(size_t index: range(value-Quarter)) {
                    float dy = (stemUp ? 1 : -1) * float(index) * (beamWidth+1) - beamWidth/2;
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
            for(size_t index: range(chord.size)) {
                const Sign& sign = chord[index];
                const Note& note = sign.note;
                if(note.accidental && ((stemUp && !shift[index]) || (!stemUp && shift[index])) ) accidentalShift[index] = 1;
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
                        if(note.step<=previousAccidentalStep+3) accidentalShift[index] = !accidentalShift[previousAccidentalIndex];
                        previousAccidentalStep = note.step;
                        previousAccidentalIndex = index;
                    }
                }
            }
#if 0
            // Shifts some more when a nearby note is dichord shifted
            for(size_t index: range(chord.size)) {
                const Sign& sign = chord[index];
                const Note& note = sign.note;
                if(note.accidental) {
                    if( ((stemUp && !shift[index]) || (!stemUp && shift[index])) ||
                            (index > 0            && abs(chord[index-1].note.step-note.step)<=1 && ((stemUp && !shift[index-1]) || (!stemUp && shift[index-1]))) ||
                            (index < chord.size-1 && abs(chord[index+1].note.step-note.step)<=1 && ((stemUp && !shift[index+1]) || (!stemUp && shift[index+1])) )) {
                        accidentalShift[index] = /*min(2,*/ accidentalShift[index]+1; //);
                    }
                }
            }
#endif

            for(size_t index: range(chord.size)) {
                Sign& sign = chord[index];
                assert_(sign.type==Sign::Note);
                Note& note = sign.note;
                const float x = X(sign) + (shift[index] ? (stemUp?1:-1)*noteSize(sign) : 0), y = Y(sign);
                float opacity = (note.tie == Note::NoTie || note.tie == Note::TieStart) ? 1 : 1./2;

                // Ledger
                ledger(sign, x);
                // Body
                {note.glyphIndex = system.glyphs.size; // Records glyph index of body, i.e next glyph to be appended to system.glyphs :
                    glyph(vec2(x, y), noteCode(sign), opacity, note.grace?6:8); }
                // Dot
                if(note.dot) {
                    float dotOffset = glyphSize(SMuFL::NoteHead::Black).x*7/6;
                    Sign betweenLines = sign; betweenLines.note.step = note.step/2*2 +1; // Aligns dots between lines
                    glyph(P(betweenLines)+vec2((shift.contains(true) ? noteSize(sign) : 0)+dotOffset,0), SMuFL::Dot, opacity);
                }

                // Accidental
                if(note.accidental) {
                    {note.accidentalGlyphIndex = system.glyphs.size; // Records glyph index of accidental, i.e next glyph to be appended to system.glyphs :
                        glyph(vec2(X(sign) - accidentalShift[index] * glyphSize(note.accidental, &smallFont).x
                                   - glyphSize(note.accidental, &smallFont).x, y), note.accidental, note.accidentalOpacity, 6);
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
                        //assert_(tieStart != invalid, sign, apply(activeTies[staff],[](TieStart o){return o.step;}), pages.size, systems.size);
                        TieStart tie = activeTies->take(tieStart);
                        //log(apply(activeTies[staff],[](TieStart o){return o.step;}));

                        assert_(tie.position.y == Y(sign));
                        float x = X(sign), y = Y(sign);
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
                        activeTies.append(staff, note.step, P(sign)+vec2(bodyOffset, 0));
                        //log(apply(activeTies[staff],[](TieStart o){return o.step;}));
                    }
                }

                // Highlight
                note.pageIndex = pageIndex;
                if(measureBars) note.measureIndex = measureBars->size();
                if(note.tie == Note::NoTie || note.tie == Note::TieStart) if(notes) notes->sorted(sign.time).append( sign );
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
                baseY = stemUp ? max(baseY, y) : min(baseY, y);
            }
        }

        // Fingering
        for(const Chord& chord: beam) {
            array<int> fingering;
            for(const Sign& sign: chord) if(sign.note.finger) fingering.append( sign.note.finger );
            if(fingering) {
                float x = X(chord[0])+noteSize(chord[0])/2, y = baseY;
                for(int finger: fingering.reverse()) { // Top to bottom
                    Font& font = textFont.font(textSize/2);
                    uint code = str(finger)[0];
                    auto metrics = font.metrics(font.index(code));
                    glyph(vec2(x-metrics.bearing.x-metrics.size.x/2,y+metrics.bearing.y), code, 1, 3, &textFont);
                    y += lineInterval;
                }
            }
        }
        beam.clear();
    }

    buffer<System::Range> System::evaluateStepRanges/*[clefs, octaveStart*/(ref<Sign> signs) {
        Clef lineClefs[staffCount] = {clefs[0], clefs[1]};
        Sign lineOctaveStart[staffCount] {octaveStart[0], octaveStart[1]};
        Range stepRanges[staffCount] = {{7, 0}, {-8, -7}};
        for(Sign sign : signs) {
            uint staff = sign.staff;
            //if(sign.type==Sign::Repeat) lineHasTopText=true;
            if(sign.type == Sign::Clef) lineClefs[staff] = sign.clef;
            else if(sign.type == Sign::OctaveShift) {
                if(sign.octave == Down) lineClefs[staff].octave++;
                else if(sign.octave == Up) lineClefs[staff].octave--;
                else if(sign.octave == OctaveStop) {
                    assert_(lineOctaveStart[staff].octave==Down || lineOctaveStart[staff].octave==Up, int(lineOctaveStart[staff].octave));
                    if(lineOctaveStart[staff].octave == Down) lineClefs[staff].octave--;
                    if(lineOctaveStart[staff].octave == Up) lineClefs[staff].octave++;
                }
                else error(int(sign.octave));
                lineOctaveStart[staff] = sign;
            }
            if(sign.type == Sign::Note) {
                sign.note.clef = lineClefs[staff]; // FIXME: postprocess MusicXML instead
                stepRanges[staff].bottom = min(stepRanges[staff].bottom, clefStep(sign));
                stepRanges[staff].top = max(stepRanges[staff].top, clefStep(sign));
            }
        }
        return copyRef(ref<Range>(stepRanges, staffCount));
    }

    // Layouts a system
        System::System(SheetContext context, float pageWidth, size_t pageIndex, size_t systemIndex, Graphics* previousSystem, ref<Sign> signs,
                       map<uint, float>* measureBars, array<TieStart>* activeTies, map<uint, array<Sign>>* notes, float spaceWidth)
            : SheetContext(context), spaceWidth(spaceWidth?:space), pageWidth(pageWidth), pageIndex(pageIndex), systemIndex(systemIndex), previousSystem(previousSystem),
            measureBars(measureBars), activeTies(activeTies), notes(notes), line(evaluateStepRanges(signs)) {

        // System Raster
        for(int staff: range(context.staffCount)) {
            for(int line: range(5)) {
                float y = staffY(staff, -line*2);
                system.lines.append(vec2(margin, y), vec2((pageWidth?pageWidth:allocatedLineWidth)-margin, y));
            }
        }

        // System first measure bar
        {float x = margin;
            vec2 min(x, staffY(1,0)), max(x, staffY(0,-8));
            system.lines.append(min, max);
            if(measureBars) measureBars->insert(signs[0].time, x);
            x += spaceWidth;
            additionnalSpaceCount++;
            timeTrack.insert(signs[0].time, {{x,x},x,x,x,x});
        }

        // Evaluates vertical bounds until next measure bar (full clear)
        size_t nextMeasureIndex = 0;
        while(nextMeasureIndex < signs.size && signs[nextMeasureIndex].type != Sign::Measure) nextMeasureIndex++;
        buffer<Range> measure = evaluateStepRanges(signs.slice(0, nextMeasureIndex));

        for(size_t signIndex: range(signs.size)) {
            Sign sign = signs[signIndex];

            auto nextStaffTime = [&](uint staff, int time) {
                assert_(staff < staffCount);
                //assert_(sign.time >= staffTime[staff]);
                if(sign.time > staffTime[staff]) {
                    uint unmarkedRestTickDuration = time - staffTime[staff];
                    uint unmarkedRestDuration = unmarkedRestTickDuration * quarterDuration / ticksPerQuarter;
                    //log("Unmarked rest", staff, beatTime[staff], unmarkedRestDuration, staffTime[staff], time, unmarkedRestTickDuration);
                    staffTime[staff] += unmarkedRestTickDuration;
                    beatTime[staff] += unmarkedRestDuration;
                    // TODO: update time track
                    //layoutNotes(staff);
                }
            };

            if(sign.type == Sign::Note||sign.type == Sign::Rest||sign.type == Sign::Clef||sign.type == Sign::OctaveShift) { // Staff signs
                uint staff = sign.staff;
                assert_(staff < staffCount, sign);
                nextStaffTime(staff, sign.time);
                float x = X(sign);

                if(sign.type == Sign::Clef || sign.type == Sign::OctaveShift) {
                    /****/ if(sign.type == Sign::Clef) {
                        Clef clef = sign.clef;
                        if(clefs[staff].clefSign != sign.clef.clefSign || clefs[staff].octave != sign.clef.octave) {
                            assert_(clefs[staff].clefSign != sign.clef.clefSign || clefs[staff].octave != sign.clef.octave);
                            float y = staffY(staff, clef.clefSign==GClef ? -6 : -2);
                            ClefSign clefSign = clef.clefSign;
                            if(clef.octave==1) clefSign = ClefSign(clefSign+SMuFL::Clef::_8va);
                            else if(clef.octave==-1) clefSign = ClefSign(clefSign+SMuFL::Clef::_8vb);
                            else assert_(clef.octave==0, clef, clef.octave);
                            x += glyph(vec2(x, y), clefSign);
                            x += space;
                        }
                        clefs[staff] = sign.clef;
                        if(signs[signIndex+1].type != Sign::Clef) timeTrack.at(sign.time).setStaves(x); // Clears other staves except if both staves change clefs
                    } else if(sign.type == Sign::OctaveShift) {
                        //log(staff, clefs[staff].octave);
                        /****/  if(sign.octave == Down) {
                            x += text(vec2(x, staffY(1, max(0,line[1].top+7))), "8"+superscript("va"), textSize, system.glyphs).x;
                            clefs[staff].octave++;
                            timeTrack.at(sign.time).octave = x;
                        } else if(sign.octave == Up) {
                            x += text(vec2(x, staffY(1, max(0,line[1].top+7))), "8"+superscript("vb"), textSize, system.glyphs).x;
                            clefs[staff].octave--;
                            timeTrack.at(sign.time).octave = x;
                        }
                        else if(sign.octave == OctaveStop) {
                            assert_(octaveStart[staff].octave==Down || octaveStart[staff].octave==Up, int(octaveStart[staff].octave));
                            if(octaveStart[staff].octave == Down) clefs[staff].octave--;
                            if(octaveStart[staff].octave == Up) clefs[staff].octave++;
                            //assert_(clefs[staff].octave == 0, staff, clefs[staff].octave, pages.size, systems.size, int(sign.octave));
                            float start = X(octaveStart[staff]) + space;
                            float end = x;
                            for(float x=start; x<end-space; x+=space*2) {
                                system.lines.append(vec2(x,staffY(1, 12)),vec2(x+space,staffY(1, 12)+1));
                            }
                        }
                        else error(int(sign.octave));
                        octaveStart[staff] = sign;
                        //log(pages.size, systems.size, sign, clefs[staff].octave);
                    } else error(int(sign.type));
                    timeTrack.at(sign.time).staffs[staff] = x;
                } else {
                    if(sign.type == Sign::Note) {
                        Note& note = sign.note;
                        note.clef = clefs[staff];

                        assert_(note.tie != Note::Merged);
                        if(!note.grace) {
                            x += glyphAdvance(SMuFL::NoteHead::Black);
                            for(Sign sign: chords[staff]) // Dichord
                                if(abs(sign.note.step-note.step) <= 1) { x += glyphAdvance(SMuFL::NoteHead::Black); break; }
                            chords[staff].insertSorted(sign);
                        } else { // Grace  note
                            log("FIXME: render grace");
                            /*
                            const float x = X(sign) - noteSize.x - glyphSize("flags.u3"_, &smallFont).x, y = Y(sign);

                            doLedger(sign);
                            // Body
                            note.glyphIndex = system.glyphs.size;
                            glyph(vec2(x, y), noteCode, note.grace?smallFont:font, system.glyphs);
                            assert_(!note.accidental);
                            // Stem
                            float stemX = x+noteSize.x-1./2;
                            system.lines.append(vec2(stemX, y-shortStemLength), vec2(stemX, y-1./2));
                            // Flag
                            glyph(vec2(stemX, y-shortStemLength), "flags.u3"_, smallFont, system.glyphs);
                            // Slash
                            float slashY = y-shortStemLength/2;
                            if(note.slash) FIXME Use SMuFL combining slash
                                        vec2(stemX +lineInterval/2, slashY -lineInterval/2),
                                        vec2(stemX -lineInterval/2, slashY +lineInterval/2));*/
                        }
                    }
                    else if(sign.type == Sign::Rest) {
                        if(sign.time != staffTime[staff]) {
                            //log("Unexpected rest start time", sign.time, staffTime[staff], pagesIndex, measureBars.size());
                        } else {
                            layoutNotes(staff);
                            if(sign.rest.value == Whole) { assert_(!pendingWhole[staff]); pendingWhole[staff] = true; }
                            else {
                                vec2 p = vec2(x, staffY(staff, -4));
                                x += glyph(p, SMuFL::Rest::Whole+int(sign.rest.value), 1./2, 6);
                            }
                            uint beatDuration = quarterDuration * 4 / timeSignature.beatUnit;
                            uint measureLength = timeSignature.beats * beatDuration;
                            uint duration = sign.rest.duration();
                            if(sign.rest.value == Whole) duration = measureLength - beatTime[staff]%measureLength;
                            beatTime[staff] += duration;
                            staffTime[staff] += duration * ticksPerQuarter / quarterDuration; //sign.duration;
                        }
                    }
                    else error(int(sign.type));

                    x += spaceWidth;
                    // Updates end position for future signs
                    for(size_t index: range(timeTrack.size())) { // Updates positions of any interleaved notes
                        if(timeTrack.keys[index] > sign.time && timeTrack.keys[index] <= sign.time+sign.duration) {
                            timeTrack.values[index].setStaves(x);
                        }
                    }
                    if(timeTrack.contains(sign.time+sign.duration)) timeTrack.at(sign.time+sign.duration).setStaves(x);
                    else timeTrack.insert(sign.time+sign.duration, {{x,x},x,x,x,x});
                }
            } else if(sign.type == Sign::Measure || sign.type==Sign::KeySignature || sign.type==Sign::TimeSignature || sign.type==Sign::Repeat) {
                // Clearing signs (across staves)
                for(size_t staff : range(staffCount)) nextStaffTime(staff, sign.time);

                if(!timeTrack.contains(sign.time)) { // FIXME
                    float x = timeTrack.values.last().maximum();
                    timeTrack.insert(sign.time, {{x,x},x,x,x,x});
                }
                assert_(timeTrack.contains(sign.time), int(sign.type));
                float x = timeTrack.at(sign.time).maximum();

                uint beatDuration = quarterDuration * 4 / timeSignature.beatUnit;
                for(size_t staff: range(staffCount)) {
                    if(beatTime[staff] % beatDuration != 0) beatTime[staff] = 0;
                }

                if(sign.type==Sign::TimeSignature) {
                    for(size_t staff: range(staffCount)) beatTime[staff] = 0;

                    timeSignature = sign.timeSignature;
                    String beats = str(timeSignature.beats);
                    String beatUnit = str(timeSignature.beatUnit);
                    float w = glyphAdvance(SMuFL::TimeSignature::_0);
                    float W = max(beats.size, beatUnit.size)*w;
                    float startX = x;
                    x = startX + (W-beats.size*w)/2;
                    for(char digit: beats) {
                        glyph(vec2(x, staffY(0, -2)), SMuFL::TimeSignature::_0+digit-'0');
                        x += glyph(vec2(x, staffY(1, -2)),SMuFL::TimeSignature::_0+digit-'0');
                    }
                    float maxX = x;
                    x = startX + (W-beatUnit.size*w)/2;
                    for(char digit: beatUnit) {
                        glyph(vec2(x, staffY(0, -6)), SMuFL::TimeSignature::_0+digit-'0');
                        x += glyph(vec2(x, staffY(1, -6)), SMuFL::TimeSignature::_0+digit-'0');
                    }
                    maxX = startX+2*glyphAdvance(SMuFL::TimeSignature::_0);
                    x += spaceWidth;
                    timeTrack.at(sign.time).setStaves(maxX); // Does not clear directions lines
                } else { // Clears all lines (including direction lines)
                    if(sign.type == Sign::Measure) {
                        if(x > pageWidth) {
                            if(!measureBars && !activeTies && !notes) {
                                assert_(!measureBars && !activeTies && !notes, x, pageWidth, signIndex, signs.size, timeTrack.size(), additionnalSpaceCount,
                                    pageWidth / timeTrack.size(), spaceWidth, space);
                                break;
                            }
                        }
                        //if(pageSize && breakIndex>startIndex && sign.measure.lineBreak) break;
                        //if(sign.measure.lineBreak == Measure::PageBreak) pageBreak = true;
                        lastMeasureBarIndex = signIndex;
                        allocatedLineWidth = x + margin;
                        spaceCount = timeTrack.size() + additionnalSpaceCount;

                        for(uint staff: range(staffCount)) layoutNotes(staff);
                        for(size_t staff: range(staffCount)) {
                            if(pendingWhole[staff]) if(measureBars) {
                                vec2 p = vec2((measureBars->values.last()+x)/2, staffY(staff, -2));
                                glyph(p, SMuFL::Rest::Whole, 1./2, 6);
                            }
                            pendingWhole[staff] = false;
                        }

                        // Measure bar
                        system.lines.append(vec2(x, staffY(1,0)), vec2(x, staffY(0,-8)));
                        if(signIndex == signs.size-1) break; // End of line, last measure bar
                        if(measureBars) measureBars->insert(sign.time, x);

                        nextMeasureIndex++;
                        while(nextMeasureIndex < signs.size && signs[nextMeasureIndex].type != Sign::Measure) nextMeasureIndex++;
                        measure = evaluateStepRanges(signs.slice(signIndex, nextMeasureIndex-signIndex));
                    }
                    else if(sign.type==Sign::KeySignature) {
                        if(sign.keySignature == 0) {
                            for(int i: range(abs(keySignature))) {
                                int step = keySignature<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
                                auto symbol = Accidental::Natural;
                                glyph(vec2(x, Y(0, {clefs[0].clefSign, 0}, step - (clefs[0].clefSign==FClef ? 14 : 0))), symbol);
                                x += glyph(vec2(x, Y(1, {clefs[1].clefSign, 0}, step - (clefs[1].clefSign==FClef ? 14 : 0))), symbol);
                            }
                        } else {
                            for(int i: range(abs(sign.keySignature))) {
                                int step = sign.keySignature<0 ? 4+(3*i+2)%7 : 6+(4*i+4)%7;
                                auto symbol = sign.keySignature<0 ? Accidental::Flat : Accidental::Sharp;
                                glyph(vec2(x, Y(0, {clefs[0].clefSign, 0}, step - (clefs[0].clefSign==FClef ? 14 : 0))), symbol);
                                x += glyph(vec2(x, Y(1, {clefs[1].clefSign, 0}, step - (clefs[1].clefSign==FClef ? 14 : 0))), symbol);
                            }
                        }
                        keySignature = sign.keySignature;
                    }
                    else if(sign.type==Sign::Repeat) {
                        if(measureBars) {
                            if(int(sign.repeat)>0) { // Ending
                                float x = measureBars->values.last();
                                text(vec2(x, staffY(1, max(0, line[1].top))), str(int(sign.repeat)), textSize, system.glyphs, vec2(0,1));
                            } else {
                                float dotX = (sign.repeat==Repeat::Begin ? measureBars->values.last()+spaceWidth/2 : x-spaceWidth/2)
                                        - glyphSize(SMuFL::Dot).x/2;
                                for(uint staff: range(staffCount)) {
                                    glyph(vec2(dotX, staffY(staff,-5)), SMuFL::Dot);
                                    glyph(vec2(dotX, staffY(staff,-3)), SMuFL::Dot);
                                }
                            }
                        }
                    }
                    else error(int(sign.type));
                    x += spaceWidth;
                    additionnalSpaceCount++; // Measure, Key, Time, Repeat adds additional spaces at a single timeTrack point
                    timeTrack.at(sign.time).setAll(x);
                }
            } else if(sign.type == Sign::Tuplet) {
                const Tuplet tuplet = sign.tuplet;
                const int middle = -4;
                int staff = tuplet.min.staff == tuplet.max.staff ? tuplet.min.staff: -1;
                int minStep = tuplet.min.step, maxStep = tuplet.max.step;
                int aboveDistance = maxStep-middle, belowDistance = middle-minStep;
                bool stemUp = staff == -1 ?: aboveDistance == belowDistance ? !staff  : aboveDistance < belowDistance;
                bool above = stemUp;

                float x0 = staffX(tuplet.first.time, Sign::Note, above ? tuplet.max.staff : tuplet.min.staff);
                float x1 = staffX(tuplet.last.time, Sign::Note, above ? tuplet.max.staff : tuplet.min.staff);
                float x = (x0+x1)/2;

                auto staffY = [&](Step step) { return Y(above ? tuplet.max.staff : tuplet.min.staff, clefs[step.staff], step.step); };
                float y0 = staffY( above ? tuplet.first.max : tuplet.first.min ) + (stemUp?-1:1)*stemLength;
                float y1 =staffY( above ? tuplet.last.max : tuplet.last.min ) + (stemUp?-1:1)*stemLength;
                float stemY = (y0 + y1) / 2;
                float dy = (above ? -1 : 1) * 2 * beamWidth;
                float y = stemY+dy;
                vec2 size = text(vec2(x,y), str(tuplet.size), textSize/2, system.glyphs, 1./2);
                if(uint(tuplet.last.time - tuplet.first.time) > ticksPerQuarter) { // No beam ? draw lines
                    system.lines.append(vec2(x0, y0+dy), vec2(x-size.x, y0+((x-size.x)-x0)/(x1-x0)*(y1-y0)+dy), black);
                    system.lines.append(vec2(x+size.x, y0+((x+size.x)-x0)/(x1-x0)*(y1-y0)+dy), vec2(x1, y1+dy), black);
                }
            }
            else { // Directions signs
                float& x = X(sign);
                if(sign.type == Sign::Metronome) {
                    if(ticksPerMinutes!=sign.metronome.perMinute*ticksPerQuarter) {
                        x += text(vec2(x, staffY(1, 12)), "♩="_+str(sign.metronome.perMinute)+" "_, textSize, system.glyphs).x;
                        if(ticksPerMinutes) log(ticksPerMinutes, "->", sign.metronome.perMinute*ticksPerQuarter); // FIXME: variable tempo
                        ticksPerMinutes = max(ticksPerMinutes, sign.metronome.perMinute*ticksPerQuarter);
                    }
                }
                else if(sign.type == Sign::Dynamic) {
                    assert_(sign.dynamic!=-1);
                    x += glyph(vec2(x, (max(staffY(1, measure[1].bottom-2), staffY(0, measure[0].top+5)) + staffY(1, measure[1].bottom-2))/2 + glyphSize(sign.dynamic).y/2 ), sign.dynamic);
                } else if(sign.type == Sign::Wedge) {
                    float y = (staffY(1, -8)+staffY(0, 0))/2;
                    if(sign.wedge == WedgeStop) {
                        bool crescendo = wedgeStart.wedge == Crescendo;
                        system.parallelograms.append( vec2(X(wedgeStart), y+(-!crescendo-1)*3), vec2(x, y+(-crescendo-1)*3), 1.f);
                        system.parallelograms.append( vec2(X(wedgeStart), y+(!crescendo-1)*3), vec2(x, y+(crescendo-1)*3), 1.f);
                    } else wedgeStart = sign;
                } else if(sign.type == Sign::Pedal) {
                    float y = staffY(0, min(-8,line[0].bottom)) + lineInterval;
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

            for(uint staff: range(staffCount)) { // Layout notes, stems, beams and ties
                array<Chord>& beam = beams[staff];
                Chord& chord = chords[staff];
                if(chord && (signIndex==signs.size-1 || signs[signIndex+1].time != chord.last().time)) { // Complete chord

                    Value value = chord[0].note.value;
                    uint chordDuration = chord[0].note.duration();
                    for(Sign sign: chord) {
                        assert_(sign.type==Sign::Note);
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
                        uint beatDuration = quarterDuration * 4 / timeSignature.beatUnit;

                        if(beamDuration+chordDuration > maximumBeamDuration /*Beam before too long*/
                                || beam.size >= 8 /*Beam before too many*/
                                || beatTime[staff]%(2*beatDuration)==0 /*Beam before spanning*/
                                || beam[0][0].note.durationCoefficientDen != chord[0].note.durationCoefficientDen // Do not mix tuplet beams
                                ) {
                            layoutNotes(staff);
                        }
                    }

                    // Beam (higher values are single chord "beams")
                    if(!beam) beamStart[staff] = beatTime[staff];
                    if(value <= Quarter) layoutNotes(staff); // Flushes any pending beams
                    if(beam) assert_(beam[0][0].note.durationCoefficientDen == chord[0].note.durationCoefficientDen,
                            beam[0][0].note.durationCoefficientDen, chord[0].note.durationCoefficientDen);
                    beam.append(copy(chord));
                    if(value <= Quarter) layoutNotes(staff); // Only stems for quarter and halfs (actually no beams :/)

                    // Next
                    chord.clear();
                    if(sign.time >= staffTime[staff]) {
                        assert_(sign.time == staffTime[staff], sign.time, staffTime[staff]);
                        staffTime[staff] += chordDuration * ticksPerQuarter / quarterDuration;
                        beatTime[staff] += chordDuration;
                    }
                }
            }
        }
        if(pedalStart) { // Draw pressed pedal line until end of line
           system.lines.append(pedalStart+vec2(0, lineInterval), vec2(pageWidth - margin, pedalStart.y + lineInterval));
        }
        int highMargin = 3, lowMargin = -8-4;
        system.bounds.min = vec2(0, staffY(1, max(highMargin, line[1].top)+7));
        system.bounds.max = vec2(pageWidth, staffY(0, min(lowMargin, line[0].bottom)-7));
    }

// Layouts notations to graphic primitives (and parses notes to MIDI keys)
Sheet::Sheet(ref<Sign> signs, uint ticksPerQuarter, int2 pageSize, float halfLineInterval, ref<uint> unused midiNotes, string title, bool pageNumbers)
    : pageSize(pageSize) {
    SheetContext context (halfLineInterval, ticksPerQuarter);
    array<System::TieStart> activeTies;
    map<uint, array<Sign>> notes;
    array<Graphics> systems;

    auto doPage = [&]{
        float totalHeight = sum(apply(systems, [](const Graphics& o) { return o.bounds.size().y; }));
		if(totalHeight < pageSize.y) { // Spreads systems with margins
			float extra = (pageSize.y - totalHeight) / (systems.size+1);
			float offset = extra;
			for(Graphics& system: systems) {
                system.translate(vec2(0, round(offset)));
                offset += system.bounds.size().y + extra;
			}
		}
		if(totalHeight > pageSize.y) { // Spreads systems without margins
			float extra = (pageSize.y - totalHeight) / (systems.size-1);
			float offset = 0;
			for(Graphics& system: systems) {
                system.translate(vec2(0, round(offset)));
                offset += system.bounds.size().y + extra;
			}
		}
		pages.append();
		for(Graphics& system: systems) {
			system.translate(system.offset); // FIXME: -> append
			if(!pageSize) assert_(!pages.last().glyphs);
			pages.last().append(system); // Appends systems first to preserve references to glyph indices
		}
        if(pages.size==1) text(vec2(pageSize.x/2,0), bold(title), context.textSize, pages.last().glyphs, vec2(1./2, 0));
        if(pageNumbers) {// Page index numbers at each corner
            float margin = context.margin;
            text(vec2(margin,margin), str(pages.size), context.textSize, pages.last().glyphs, vec2(0, 0));
            text(vec2(pageSize.x-margin,margin), str(pages.size), context.textSize, pages.last().glyphs, vec2(1, 0));
            text(vec2(margin,pageSize.y-margin/2), str(pages.size), context.textSize, pages.last().glyphs, vec2(0, 1));
            text(vec2(pageSize.x-margin,pageSize.y-margin/2), str(pages.size), context.textSize, pages.last().glyphs, vec2(1, 1));
		}
		systems.clear();
	};

    size_t lineCount = 0; float requiredHeight = 0;
    for(size_t startIndex = 0; startIndex < signs.size;) { // Lines
        size_t breakIndex;
        float spaceWidth;
        {// Evaluates next line break
            System system(context, pageSize.x, pages.size, systems.size, systems ? &systems.last() : nullptr, signs.slice(startIndex), 0, 0, 0, 0);
            breakIndex = startIndex+system.lastMeasureBarIndex+1;
            assert_(startIndex < breakIndex && system.spaceCount, startIndex, breakIndex, system.spaceCount);
            spaceWidth = system.space + (pageSize ? float(pageSize.x - system.allocatedLineWidth) / float(system.spaceCount) : 0);
        }

		// Breaks page as needed
        //int highMargin = 3, lowMargin = -8-4;
        {
            //float minY = staffY(1, max(highMargin,currentLineHighestStep)+7);
            //float maxY = staffY(0, min(lowMargin, currentLineLowestStep)-7);
            if(systems && (/*pageBreak ||*/ systems.size >= 3 /*requiredHeight + (maxY-minY) > pageSize.y*/)) {
                requiredHeight = 0; // -> doPage
                doPage();
            }
        }

        // -- Layouts justified system
        System system(context, pageSize.x, pages.size, systems.size, systems ? &systems.last() : nullptr, signs.slice(startIndex, breakIndex-startIndex),
                      &measureBars, &activeTies, &notes, spaceWidth);
        context = system; // Copies back context for next line

        lineCount++;
        requiredHeight += system.system.bounds.size().y;
        systems.append(move(system.system));
        this->lowestStep = min(this->lowestStep, system.line[0].bottom);
        this->highestStep = max(this->highestStep, system.line[1].top);

        startIndex = breakIndex;
	}
	doPage();
#if 0
    if(!ticksPerMinutes) ticksPerMinutes = 120*ticksPerQuarter;

    measureBars.insert(/*t*/0u, /*x*/0.f);

	midiToSign = buffer<Sign>(midiNotes.size, 0);
	array<uint> chordExtra;

    constexpr bool logErrors = false;
	if(midiNotes) while(chordToNote.size < notes.size()) {
		if(!notes.values[chordToNote.size]) {
			chordToNote.append( midiToSign.size );
			if(chordToNote.size==notes.size()) break;
			array<Sign>& chord = notes.values[chordToNote.size];
			chordExtra.filter([&](uint midiIndex){ // Tries to match any previous extra to next notes
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
				return true; // Discards
			});
			if(chordExtra) {
				if(logErrors) log("+"_+str(apply(chordExtra, [&](const uint index){return midiNotes[index];})));
				if(!(chordExtra.size<=2)) { log("chordExtra.size<=2"); break; }
				assert_(chordExtra.size<=2, chordExtra.size);
				extraErrors+=chordExtra.size;
				chordExtra.clear();
			}
			if(!notes.values[chordToNote.size]) chordToNote.append( midiToSign.size );
			assert_(chordToNote.size<notes.size());
			if(chordToNote.size==notes.size()) break;
		}
		assert_(chordToNote.size<notes.size());
		array<Sign>& chord = notes.values[chordToNote.size];
		assert_(chord);

		uint midiIndex = midiToSign.size;
		assert_(midiIndex < midiNotes.size);
		uint midiKey = midiNotes[midiIndex];

		if(extraErrors > 40 /*FIXME: tremolo*/ || wrongErrors > 9 || missingErrors > 13 || orderErrors > 8) {
		//if(extraErrors || wrongErrors || missingErrors || orderErrors) {
			log(midiIndex, midiNotes.size);
			log("MID", midiNotes.slice(midiIndex,7));
			log("XML", chord);
			break;
		}

		int match = chord.indexOf(midiKey);
		if(match >= 0) {
			Sign sign = chord.take(match); Note note = sign.note;
			assert_(note.key() == midiKey);
			midiToSign.append( sign );
			if(note.pageIndex != invalid && note.glyphIndex != invalid) {
				vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
				text(p+vec2(space, 2), str(note.key()), 12, debug->glyphs);
			}
		} else if(chordExtra && chord.size == chordExtra.size) {
			int match = notes.values[chordToNote.size+1].indexOf(midiNotes[chordExtra[0]]);
			if(match >= 0) {
				assert_(chord.size<=3/*, chord*/);
				if(logErrors) log("-"_+str(chord));
				missingErrors += chord.size;
				chord.clear();
				chordExtra.filter([&](uint index){
					if(!notes.values[chordToNote.size]) chordToNote.append( midiToSign.size );
					array<Sign>& chord = notes.values[chordToNote.size];
					assert_(chord, chordToNote.size, notes.size());
					int match = chord.indexOf(midiNotes[index]);
					if(match<0) return false; // Keeps as extra
					midiKey = midiNotes[index];
					Sign sign = chord.take(match); Note note = sign.note;
					assert_(midiKey == note.key());
					midiToSign[index] = sign;
					if(note.pageIndex != invalid && note.glyphIndex != invalid) {
						vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
						text(p+vec2(space, 2), str(note.key()), 12, debug->glyphs);
					}
					return true; // Discards extra as matched to next chord
				});
			} else {
				assert_(midiKey != chord[0].note.key());
				uint previousSize = chord.size;
				chord.filter([&](const Sign& sign) { Note note = sign.note;
					if(midiNotes.slice(midiIndex,5).contains(note.key())) return false; // Keeps as extra
					uint midiIndex = chordExtra.take(0);
					uint midiKey = midiNotes[midiIndex];
					assert_(note.key() != midiKey);
					if(logErrors) log("!"_+str(note.key(), midiKey));
					if(note.pageIndex != invalid && note.glyphIndex != invalid) {
						vec2 p = pages[note.pageIndex].glyphs[note.glyphIndex].origin;
						text(p+vec2(space, 2), str(note.key())+"?"_+str(midiKey)+"!"_, 12, debug->glyphs);
					}
					wrongErrors++;
					return true; // Discards as wrong
				});
				if(previousSize == chord.size) { // No notes have been filtered out as wrong, remaining are extras
					assert_(chordExtra && chordExtra.size<=3, chordExtra.size);
					if(logErrors) log("+"_+str(apply(chordExtra, [&](const uint index){return midiNotes[index];})));
					extraErrors += chordExtra.size;
					chordExtra.clear();
				}
			}
		} else {
			midiToSign.append({.note={}});
			chordExtra.append( midiIndex );
		}
	}
    if(chordToNote.size == notes.size() || !midiNotes) {}
	else { firstSynchronizationFailureChordIndex = chordToNote.size; }
	if(logErrors && (extraErrors||wrongErrors||missingErrors||orderErrors)) log(extraErrors, wrongErrors, missingErrors, orderErrors);
#endif
}

inline bool operator ==(const Sign& sign, const uint& key) {
	assert_(sign.type == Sign::Note);
	return sign.note.key() == key;
}

vec2 Sheet::sizeHint(vec2) { return vec2(measureBars.values.last(), 0/*FIXME*/); }

shared<Graphics> Sheet::graphics(vec2, Rect) { return shared<Graphics>(&pages[pageIndex]); }

size_t Sheet::measureIndex(float x) {
	if(x < measureBars.values[0]) return invalid;
	for(size_t i: range(measureBars.size()-1)) if(measureBars.values[i]<=x && x<measureBars.values[i+1]) return i;
	assert_(x >= measureBars.values.last()); return measureBars.size();
}

bool Sheet::keyPress(Key key, Modifiers) {
	if(key==LeftArrow) { pageIndex = max(0, int(pageIndex)-1); return true; }
	if(key==RightArrow) { pageIndex = min(pageIndex+1, pages.size-1); return true; }
	return false;
}
