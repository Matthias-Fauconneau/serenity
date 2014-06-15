#include "music.h"
#include "thread.h"
#include "window.h"
#include "font.h"
#include "utf8.h"
#include "midi.h"

//#include "MusicXML.cc"
array<Sign> parse(string text, uint& divisions);

struct MusicXML : Widget {
    // MusicXML
    uint divisions = 256; // Time steps per measure
    array<Sign> signs;    
    array<Sign> notes; // Signs for notes (currently only using time)

    // MIDI
    MidiFile midi;
    array<uint> midiToBlit; // Blit index for each MIDI index

    Window window {this, int2(0, 384), "MusicXML"_};
    MusicXML() : midi(readFile("Storytime.mid"_, Folder("Scores"_,home()))) {
        signs = parse(readFile("Storytime.xml"_, Folder("Scores"_,home())), divisions);

        layout();
        synchronize();

        window.background = Window::White;
        window.actions[Escape] = []{exit();};
        window.show();
    }

    // Layout
    const int staffCount = 2;
    const int halfLineInterval = 5, lineInterval = 2*halfLineInterval;
    const int lineWidth = 1, stemWidth = 1, stemLength = 4*lineInterval, beamWidth = 6;
    const int shortStemLength = 5*halfLineInterval;

    int clefStep(ClefSign clefSign, int step) { return step - (clefSign==Treble ? 10 : -2); } // Translates C4 step to top line step using clef
    int staffY(uint staff, int clefStep) { return staff*10*lineInterval - clefStep * halfLineInterval; } // Clef independent
    int Y(Clef clef, uint staff, int step) { return staffY(staff, clefStep(clef.clefSign, step)); } // Clef dependent
    int Y(Sign sign) { assert_(sign.type==Sign::Note); return Y(sign.note.clef, sign.staff, sign.note.step); } // Clef dependent
    int Y(const map<uint, Clef>& clefs, uint staff, int step) { return staffY(staff, clefStep(clefs.at(staff).clefSign, step)); } // Clef dependent

    Font graceFont {File("emmentaler-26.otf"_, Folder("Scores"_,home())), 4*halfLineInterval};
    Font font {File("emmentaler-26.otf"_, Folder("Scores"_,home())), 9*halfLineInterval};
    Font textFont{File("FreeSerifBold.ttf"_,Folder("Scores"_,home())), 6*halfLineInterval};
    Font smallFont{File("FreeSerifBold.ttf"_,Folder("Scores"_,home())), 14};
    vec2 glyphSize(const string name) { return font.size(font.index(name)); }
    //float advance(const string name) { return font.advance(font.index(name)); }
    int2 noteSize = int2(round(glyphSize("noteheads.s2"_)));

    // Control
    array<int> measures; // X position of measure starts

    // Render
    array<Rect> fills;
    struct Parallelogram {
        int2 min,max; int dy;
    };
    array<Parallelogram> parallelograms;
    struct Blit {
        int2 position;
        Image image;
    };
    array<Blit> blits;
    typedef array<vec2> Cubic;
    array<Cubic> cubics;

    map<uint, vec3> colors; // Overrides color for Blit index
    uint position = 0; //200-1;
    uint unorderedErrors = 0, extraErrors = 0, missingErrors = 0, wrongErrors = 0;

    float glyph(int2 position, const string name, Font& font) {
        uint16 index = font.index(name);
        const Glyph& glyph = font.glyph(index);
        blits << Blit{position+glyph.offset, share(glyph.image)}; // Lifetime of font
        return font.advance(index);
    }
    float glyph(int2 position, const string name) { return glyph(position, name, font); }

    uint text(int2 position, const string& text, Font& font) {
        uint x = position.x;
        for(uint code: toUTF32(text)) {
            uint16 index = font.index(code);
            const Glyph& glyph = font.glyph(index);
            blits << Blit{int2(x, position.y)+glyph.offset, share(glyph.image)};
            x += font.advance(index);
        }
        return x;
    }

    // Layouts notations to graphic primitives (and parses notes to MIDI keys)
    void layout() {
        int x = 0;
        map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0};
        typedef array<Sign> Chord; // Signs belonging to a same chord (same time)
        Chord chords[2]; // Current chord (per staff)
        array<Chord> beams[2]; // Chords belonging to current beam (per staff) (also for correct single chord layout)
        array<array<Sign>> pendingSlurs[2];
        array<Sign> slurs[2]; // Signs belonging to current slur (pending slurs per staff)
        uint pedalStart = 0; // Last pedal start/change position
        Sign wedgeStart; // Current wedge
        struct Position { // Holds positions for both notes (default) and directions (explicit)
            int direction, note;
            Position(int x) : direction(x), note(x) {}
            void operator=(int x) { note=max(note,x); }
            operator int() const { return note; }
        };
        map<uint, Position> timeTrack; // Maps times to positions
        auto X = [&](const Sign& sign) { return timeTrack.at(sign.time); };
        auto P = [&](const Sign& sign) { return int2(X(sign),Y(sign)); };
        // System
        vec2 p0 = vec2(x+noteSize.x, staffY(0, 0));
        vec2 p1 = vec2(x+noteSize.x, staffY(1, -8));
        vec2 pM = vec2(x, (p0+p1).y/2);
        vec2 c0[2] = {vec2(pM.x, p0.y), vec2((pM+p0).x/2, p0.y)};
        vec2 c0M[2] = {vec2((pM+p0).x/2, pM.y), vec2(p0.x, pM.y)};
        vec2 c1M[2] = {vec2((pM+p1).x/2, pM.y), vec2(p1.x, pM.y)};
        vec2 c1[2] = {vec2(pM.x, p1.y), vec2((pM+p1).x/2, p1.y)};
        cubics << Cubic(ref<vec2>({p0,c0[0],c0M[0],pM,c1M[0],c1[0],p1,c1[1],c1M[1],pM,c0M[1],c0[1]}));
        x += noteSize.x;
        fills << Rect(int2(x-1, staffY(0, 0)), int2(x+1, staffY(1, -8)));
        measures << x;
        for(Sign& sign: signs) {
            // Layout accidentals
            Chord& chord = chords[sign.staff];
            if(chord && sign.time != chord.last().time) {
                int lastY = -1000, dx = 0;
                for(Sign sign: chord.reverse()) {
                    if(!sign.note.accidental) continue;
                    int y = Y(sign);
                    if(abs(y-lastY)<=lineInterval) dx -= noteSize.x;
                    else dx = 0;
                    glyph(int2(X(sign)-noteSize.x+dx, y), "accidentals."_+ref<string>({"flat"_,"sharp"_,"natural"_})[sign.note.accidental-1]);
                    lastY = y;
                }
                chord.clear();
            }

            // Layout tails and beams
            array<Chord>& beam = beams[sign.staff];
            if(beam && (
                        (sign.time%(timeSignature.beats*divisions) == (timeSignature.beats*divisions)/2 && sign.time>beam[0][0].time) || // Beam at half measure
                        (beam[0][0].time%divisions && sign.time>beam[0][0].time) || // Off beat (stem after complete chord)
                        (sign.type == Sign::Rest || sign.type == Sign::Measure) ||
                        (sign.type == Sign::Note && (beam.last().last().note.duration< Eighth || sign.note.duration<Eighth) && sign.time>beam[0][0].time))) {
                int sum = 0, count=0; for(const Chord& chord: beam) { for(Sign sign: chord) sum += clefStep(sign.note.clef.clefSign, sign.note.step); count+=chord.size; }
                bool stemUp = sum < -4*count; // sum/count<-4 (Average note height below mid staff)
                int dx = (stemUp ? noteSize.x - 2 : 0);
                int dy = (stemUp ? 0 : 0);

                if(beam.size==1) { // Draws single stem
                    Sign sign = stemUp ? beam[0].last() : beam[0].first();
                    int x = X(sign) + dx;
                    int yMin = Y(sign.note.clef, sign.staff, beam[0].first().note.step);
                    int yMax = Y(sign.note.clef, sign.staff, beam[0].last().note.step);
                    int yBase = stemUp ? yMin : yMax + dy;
                    int yStem = stemUp ? min(yMax-stemLength, staffY(sign.staff, -4)) : max(yMin+stemLength, staffY(sign.staff, -4));
                    fills << Rect(int2(x, min(yBase, yStem)), int2(x+stemWidth, max(yBase, yStem)));
                    /**/ if(sign.note.duration==Eighth) glyph(int2(x+stemWidth, yStem), stemUp?"flags.u3"_:"flags.d3"_);
                    else if(sign.note.duration==Sixteenth) glyph(int2(x+stemWidth, yStem), stemUp?"flags.u4"_:"flags.d4"_);
                } else if(beam.size==2) { // Draws slanted beam
                    int x[2], base[2], tip[2];
                    for(uint i: range(2)) {
                        const Chord& chord = beam[i];
                        Sign sign = chord.first();
                        x[i] = X(sign) + dx;
                        base[i] = Y(sign.note.clef, sign.staff, (stemUp?chord.first():chord.last()).note.step) + dy;
                        tip[i] = Y(sign.note.clef, sign.staff, (stemUp?chord.last():chord.first()).note.step)+(stemUp?-1:1)*(stemLength-noteSize.y/2);
                    }
                    int farTip = stemUp ? min(tip[0],tip[1]) : max(tip[0],tip[1]);
                    int delta[2] = {clip(-lineInterval, tip[0]-farTip, lineInterval), clip(-lineInterval, tip[1]-farTip, lineInterval)};
                    farTip = stemUp ? min(farTip, staffY(sign.staff, -4)) : max(farTip, staffY(sign.staff, -4));
                    for(uint i: range(2)) fills << Rect(int2(x[i], min(base[i],farTip+delta[i])),int2(x[i]+stemWidth, max(base[i],farTip+delta[i])));
                    Sign sign[2] = { stemUp?beam.first().last():beam.first().first(), stemUp?beam.last().last():beam.last().first()};
                    int2 p0 (X(sign[0])+dx, farTip+delta[0]-beamWidth/2);
                    int2 p1 (X(sign[1])+dx+stemWidth, farTip+delta[1]-beamWidth/2);
                    parallelograms << Parallelogram{p0, p1, beamWidth};
                } else { // Draws horizontal beam
                    int stemY = stemUp ? -1000 : 1000; //FIXME
                    if(stemUp) {
                        for(const Chord& chord: beam) for(Sign sign: chord) stemY = max(stemY, Y(sign)-stemLength);
                        for(const Chord& chord: beam) for(Sign sign: chord) stemY = min(stemY, Y(sign)-shortStemLength);
                    } else {
                        for(const Chord& chord: beam) for(Sign sign: chord) stemY = min(stemY, Y(sign)+stemLength);
                        for(const Chord& chord: beam) for(Sign sign: chord) stemY = max(stemY, Y(sign)+shortStemLength);
                    }
                    stemY = stemUp ? min(stemY, staffY(sign.staff, -4)) : max(stemY, staffY(sign.staff, -4));
                    for(const Chord& chord: beam) for(Sign sign: chord) {
                        int x = X(sign) + dx;
                        int y = Y(sign) + dy;
                        fills << Rect(int2(x,min(y, stemY)),int2(x+stemWidth, max(stemY, y)));
                    }
                    fills << Rect(int2(X(beam.first()[0]) + dx,             stemY-beamWidth/2+1),
                                  int2(X(beam.last ()[0]) + dx + stemWidth, stemY+beamWidth/2));
                }

                for(const Chord& chord: beam) {
                    Sign sign = stemUp ? chord.first() : chord.last();
                    int x = X(sign) + noteSize.x/2;
                    int step = clefStep(sign.note.clef.clefSign, sign.note.step);
                    int y = Y(sign) + (stemUp?1:-1) * (lineInterval+(step%2?0:halfLineInterval));
                    if(chord.first().note.staccato) { glyph(int2(x,y),"scripts.staccato"_); y+=lineInterval; }
                    if(chord.first().note.tenuto) { glyph(int2(x,y),"scripts.tenuto"_); y+=lineInterval; }
                    int y2 = staffY(sign.staff, stemUp ? -10 : 2);
                    y = stemUp ? max(y,y2) : min(y,y2);
                    y -= (stemUp?0:glyphSize("scripts.sforzato"_).y/2);
                    if(chord.first().note.accent) { glyph(int2(x,y),"scripts.sforzato"_); y+=lineInterval; }
                 }
                beam.clear();

                for(const array<Sign>& slur: pendingSlurs[sign.staff]) {
                    int sum = 0; for(Sign sign: slur) sum += clefStep(sign.note.clef.clefSign, sign.note.step);
                    int slurDown = (int(slur.size) > count ? sum < (int(slur.size) * -4) : stemUp) ? 1 : -1;

                    int y = slurDown>0 ? -1000 : 1000;
                    for(Sign sign: slur) {
                        y = slurDown>0 ? max(y, Y(sign)) : min(y, Y(sign));
                    }
                    Sign first = slur.first(); if(slurDown<0) for(Sign sign: slur) if(sign.time==first.time) first=sign; // Top note of chord
                    Sign last = slur.last(); if(slurDown>0) for(int i=slur.size-1; i>=0; i--) if(slur[i].time==last.time) last=slur[i]; // Bottom note of chord

                    vec2 p0 = vec2(P(first)) + vec2(noteSize.x/2, slurDown*1*noteSize.y);
                    vec2 p1 = vec2(P(last)) + vec2(noteSize.x/2, slurDown*2*noteSize.y);
                    vec2 k0 = vec2(p0.x, y) + vec2(0, slurDown*2*noteSize.y);
                    vec2 k0p = k0 + vec2(0, slurDown*noteSize.y/2);
                    vec2 k1 = vec2(p1.x, y) + vec2(0, slurDown*2*noteSize.y);
                    vec2 k1p = k1 + vec2(0, slurDown*noteSize.y/2);
                    cubics << Cubic(ref<vec2>({p0,k0,k1,p1,k1p,k0p}));
                }
                pendingSlurs[sign.staff].clear();
            }

            uint staff = sign.staff;
            if(timeTrack.contains(sign.time)) x = timeTrack.at(sign.time); // Synchronizes with previously laid signs
            else timeTrack.insert(sign.time, x); // Marks position for future signs

            /**/ if(sign.type == Sign::Note) {
                Note& note = sign.note;
                assert_(note.clef.octave == clefs.at(sign.staff).octave); // FIXME: key relies on correct octave`
                note.clef = clefs.at(sign.staff);
                int2 p = int2(x, Y(sign));
                Duration duration = note.duration;
                note.blitIndex = blits.size;
                int dx = glyph(p, "noteheads.s"_+dec(min(2,int(duration))), note.grace?graceFont:font);
                int step = clefStep(note.clef.clefSign, note.step);
                for(int s=2; s<=step; s+=2) { int y=staffY(staff, s); fills << Rect(int2(x-dx/3,y),int2(x+dx*4/3,y+1)); }
                for(int s=-10; s>=step; s-=2) { int y=staffY(staff, s); fills << Rect(int2(x-dx/3,y),int2(x+dx*4/3,y+1)); }
                if(note.slash) parallelograms << Parallelogram{p+int2(-dx+dx/2,dx), p+int2(dx+dx/2,-dx), 1};
                if(note.dot) glyph(p+int2(dx*4/3,0),"dots.dot"_);
                x += 3*dx;

                chord.insertSorted(sign);

                if(duration>=Half) { if(beam && beam.last().last().time == sign.time) beam.last().insertSorted(sign); else beam << Chord(ref<Sign>({sign})); }

                array<Sign>& slur = slurs[sign.staff];
                if(slur) slur << sign;
                if(note.slur) {
                    if(!slur) slur << sign; // Starts new slur (only if visible)
                    else { pendingSlurs[sign.staff] << move(slur); assert_(!slur); } // Stops
                }

                if(note.tie == Note::NoTie || note.tie == Note::TieStart) notes << sign;
            }
            else if(sign.type == Sign::Rest) {
                int2 p = int2(x, staffY(staff, -4));
                if(sign.rest.duration == Whole) x+= 3*glyph(p, "rests.0"_);
                else if(sign.rest.duration == Half) x+= 3*glyph(p, "rests.1"_);
                else if(sign.rest.duration == Quarter) x+= 3*glyph(p, "rests.2"_);
                else if(sign.rest.duration == Eighth) x+= 3*glyph(p, "rests.3"_);
                else if(sign.rest.duration == Sixteenth) x+= 3*glyph(p, "rests.4"_);
                else error(int(sign.rest.duration));
            }
            else if(sign.type == Sign::Measure) {
                //if(sign.staff==1 && x > int(target.width) && !slurs[0] && !slurs[1]) break;
                if(sign.staff==0) {
                    fills << Rect(int2(x-1, staffY(0,0)),int2(x+1, staffY(1,-8))); // Bar
                    // Raster
                    for(int staff: range(staffCount)) {
                        for(int line: range(5)) {
                            int y = staffY(staff, -line*2);
                            fills << Rect(int2(measures.last(), y), int2(x, y+lineWidth));
                        }
                    }
                    measures << x;
                    x += 2*noteSize.x;
                    timeTrack.at(sign.time).direction = x;
                    uint sx = x;
                    for(uint8 code: dec(sign.measure.index)) {
                        uint16 index = textFont.index(code);
                        const Glyph& glyph = textFont.glyph(index);
                        blits << Blit{int2(sx, staffY(0, 16))+glyph.offset, share(glyph.image)};
                        sx += textFont.advance(index);
                    }
                }
            }
            else if(sign.type == Sign::Pedal) {
                int y = staffY(1, -24);
                if(sign.pedal.action == Ped) glyph(int2(x, y), "pedal.Ped"_);
                if(sign.pedal.action == Start) pedalStart = x + glyphSize("pedal.Ped"_).x;
                if(sign.pedal.action == Change || sign.pedal.action == PedalStop) {
                    fills << Rect(int2(pedalStart, y), int2(x, y+1));
                    if(sign.pedal.action == PedalStop) fills << Rect(int2(x-1, y-lineInterval), int2(x, y));
                    else {
                        parallelograms << Parallelogram{int2(x, y-1), int2(x+noteSize.x/2, y-noteSize.x), 2};
                        parallelograms << Parallelogram{int2(x+noteSize.x/2, y-noteSize.x), int2(x+noteSize.x, y), 2};
                        pedalStart = x + noteSize.x;
                    }
                }
            }
            else if(sign.type == Sign::Wedge) {
                uint x = timeTrack.at(sign.time).direction;
                int y = (staffY(0, -8)+staffY(1, 0))/2;
                if(sign.wedge.action == WedgeStop) {
                    bool crescendo = wedgeStart.wedge.action == Crescendo;
                    parallelograms << Parallelogram{int2(timeTrack.at(wedgeStart.time).direction, y+(-!crescendo-1)*3), int2(x, y+(-crescendo-1)*3), 1};
                    parallelograms << Parallelogram{int2(timeTrack.at(wedgeStart.time).direction, y+(!crescendo-1)*3), int2(x, y+(crescendo-1)*3), 1};
                } else wedgeStart = sign;
            }
            else if(sign.type == Sign::Dynamic) {
                string word = ref<string>({"ppp"_,"pp"_,"p"_,"mp"_,"mf"_,"f"_,"ff"_,"fff"_})[uint(sign.dynamic.loudness)];
                float w = 0; for(char character: word.slice(0,word.size-1)) w += font.advance(font.index(string{character})); w += glyphSize({word.last()}).x;
                int& x = timeTrack.at(sign.time).direction;
                x -= w/2; x += glyphSize({word.first()}).x/2;
                for(char character: word) {
                    x += glyph(int2(x, (staffY(0, -8)+staffY(1, 0))/2), {character});
                }
            } else if(sign.type == Sign::Clef) {
                string change = clefs.contains(sign.staff)?"_change"_:""_;
                Clef clef = sign.clef;
                assert_(!clef.octave);
                if(!clefs.contains(sign.staff) || clefs.at(sign.staff).clefSign != sign.clef.clefSign) {
                    clefs[sign.staff] = sign.clef;
                    x += noteSize.x;
                    if(clef.clefSign==Treble) x += glyph(int2(x, Y(clefs,staff,4)), "clefs.G"_+change);
                    if(clef.clefSign==Bass) x += glyph(int2(x, Y(clefs,staff,-4)),"clefs.F"_+change);
                    x += noteSize.x;
                    if(staff==0) x=X(sign);
                    timeTrack.at(sign.time).direction = x;
                }
            }
            else if(sign.type==Sign::KeySignature) {
                keySignature = sign.keySignature;
                int fifths = keySignature.fifths;
                for(int i: range(abs(fifths))) {
                    int step = (fifths>0?2:4) + ((fifths>0 ? 4 : 3) * i +2)%7;
                    glyph(int2(x, Y(clefs,0,step)), fifths<0?"accidentals.flat"_:"accidentals.sharp"_);
                    x += glyph(int2(x, Y(clefs,1,step - (clefs[1u].clefSign==Bass ? 14 : 0))), fifths<0?"accidentals.flat"_:"accidentals.sharp"_);
                }
                x += noteSize.x;
                timeTrack.at(sign.time).direction = x;
            }
            else if(sign.type==Sign::TimeSignature) {
                timeSignature = sign.timeSignature;
                static constexpr string numbers[10] = {"zero"_,"one"_,"two"_,"three"_,"four"_,"five"_,"six"_,"seven"_,"eight"_,"nine"_};
                glyph(int2(x, staffY(0, -4)),numbers[timeSignature.beats]);
                glyph(int2(x, staffY(1, -4)),numbers[timeSignature.beats]);
                glyph(int2(x, staffY(0, -8)),numbers[timeSignature.beatUnit]);
                x += 2*glyph(int2(x, staffY(1, -8)),numbers[timeSignature.beatUnit]);
            }
            else if(sign.type == Sign::Metronome) {
                text(int2(x, staffY(0, 16)), "♩="_+dec(sign.metronome.perMinute), textFont);
            }

            if(timeTrack.contains(sign.time+sign.duration)) timeTrack.at(sign.time+sign.duration) = x; // Updates end position for future signs
            else timeTrack.insert(sign.time+sign.duration, x);
        }        
    }

    int measureIndex(int x0) {
        for(uint i: range(measures.size-1)) if(measures[i]<=x0 && x0<measures[i+1]) return i; return -1;
    }

    /// Returns index of note matching key between notes played at the same time as \a noteIndex (-1 if none found)
    int find(const array<Sign>& notes, uint key) {
        uint time = notes[0].time;
        for(uint index: range(notes.size)) {
            Sign sign = notes[index];
            assert_(sign.time >= time);
            if(sign.time > time) break;
            if(sign.note.key == key) return index;
        }
        return -1;
    }

    void synchronize() {
#if 0
        for(Sign sign: notes) {
            int2 p = blits[sign.note.blitIndex].position;
            text(p+int2(noteSize.x, 2), str(sign.note.key), smallFont);
        }
        position = measures[13-1];
#else
        array<Sign> notes = copy(this->notes);
        while(notes) {
            uint midiIndex = midiToBlit.size;
            uint midiKey = midi.notes[midiIndex].key;
            Note note; note.blitIndex=0, note.key=0; // DEBUG
            String debug;
            int match = find(notes, midiKey);
            if(match >= 0) {
                note = notes.take(match).note;
                assert_(note.key == midiKey);
                midiToBlit << note.blitIndex;
                //if(match>0) { log("Unordered MIDI note"); debug<<'!'; unorderedErrors++; }
            } else {
                log(apply(midi.notes.slice(midiIndex,4), [](const MidiNote& note){return note.key;}));
                log(apply(notes.slice(0,4), [](const Sign& sign){return sign.note.key;}));

                int nextMatch = find(notes, midi.notes[midiIndex+1].key);
                assert_(nextMatch >= 0, "Double fault"_);
                note = notes[nextMatch].note;
                midiToBlit << -1;
                debug << '!';
                if(nextMatch==0) { log("Extra MIDI note"_); debug<<'+'; extraErrors++; }
                else if(nextMatch==1) {
                    log("Wrong MIDI note"); debug<<'!'; wrongErrors++;
                    notes.removeAt(nextMatch);
                }
            }
            int2 p = blits[note.blitIndex].position;
            text(p+int2(noteSize.x, 2), debug+str(note.key, midiKey), smallFont);
            if(debug) position = measures[max(0,measureIndex(p.x)-1)];
            if(extraErrors>0 || wrongErrors>2 || missingErrors>0) break;
        }
#endif
    }

    // Render
    int2 sizeHint() { return int2(-1, staffY(1,-16)); }
    void render(const Image& target) {
        int2 offset = int2(-position, (target.height - sizeHint().y)/2 + 4*lineInterval);
        // TODO: cull
        for(Rect r: fills) fill(target, offset+r);
        for(Parallelogram p: parallelograms) parallelogram(target, offset+p.min, offset+p.max, p.dy);
        for(uint i: range(blits.size)) { const Blit& b=blits[i]; blit(target, offset+b.position, b.image, colors.value(i, black)); }
        for(const Cubic& c: cubics) { buffer<vec2> points(c.size); for(uint i: range(c.size)) points[i]=vec2(offset)+c[i]; cubic(target, points); }
    }

    bool mouseEvent(int2, int2, Event, Button button) {
        uint index = measureIndex(position);
        if(button==WheelUp && int(index)>0) { position=measures[index-1]; return true; } //TODO: previous measure
        if(button==WheelDown && uint(index)<measures.size-1) { position=measures[index+1]; return true; } //TODO: next measure
        return false;
    }
} app;
