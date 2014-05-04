#include "music.h"
#include "thread.h"
#include "window.h"
#include "font.h"
#include "utf8.h"

//#include "MusicXML.cc"
array<Sign> parse(string text, uint& divisions);

struct MusicXML : Widget {
    // MusicXML
    uint divisions = 256; // Time steps per measure
    array<Sign> signs;

    Window window {this, int2(0, 384), "MusicXML"_};
    MusicXML() {
        signs = parse(readFile("Storytime.xml"_, Folder("Scores"_,home())), divisions);
        layout();
        position = measures[23];
        window.background = Window::White;
        window.actions[Escape] = []{exit();};
        window.show();
    }

    // Layout
    const int halfLineInterval = 4, lineInterval = 2*halfLineInterval;
    const int staffCount = 2;

    Font font {File("emmentaler-26.otf"_, Folder("Scores"_,home())), 4*lineInterval};
    vec2 glyphSize(const string name) { return font.size(font.index(name)); }

    int2 noteSize = int2(round(glyphSize("noteheads.s2"_)));
    const int lineWidth = 1;
    const int tailWidth = lineWidth /*halfLineInterval/4*/, tailLength = 3*noteSize.y;

    float advance(const string name) { return font.advance(font.index(name)); }

    int clefStep(ClefSign clefSign, int step) { return step - (clefSign==Treble ? 10 : -2); } // Translates C4 step to top line step using clef
    int staffY(uint staff, int clefStep) { return staff*10*lineInterval - clefStep * halfLineInterval; } // Clef independent
    int Y(Clef clef, uint staff, int step) { return staffY(staff, clefStep(clef.clefSign, step)); } // Clef dependent
    int Y(Sign sign) { assert_(sign.type==Sign::Note); return Y(sign.note.clef, sign.staff, sign.note.step); } // Clef dependent
    int Y(const map<uint, Clef>& clefs, uint staff, int step) { return staffY(staff, clefStep(clefs.at(staff).clefSign, step)); } // Clef dependent

    Font textFont{File("FreeSerifBold.ttf"_,Folder("Scores"_,home())), 6*halfLineInterval};

    // Control
    array<int> measures; // X position of measures
    array<uint> noteToBlit; // Blit index for each note index

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

    float glyph(int2 position, const string name) {
        uint16 index = font.index(name);
        const Glyph& glyph = font.glyph(index);
        blits << Blit{position+glyph.offset, share(glyph.image)}; // Lifetime of font
        return font.advance(index);
    }

    // Layouts first without immediate rendering for control (seek, ...)
    void layout() {
        int x = 0;
        map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0};
        typedef array<Sign> Chord; // Signs belonging to a same chord (same time)
        array<Chord> beams[2]; // Chords belonging to current beam (per staff) (also for correct single chord layout)
        array<Sign> slurs[2]; // Signs belonging to current slur (per staff)
        uint pedalStartTime = 0; // Current pedal start time
        Sign wedgeStart; // Current wedge
        struct Position { // Holds positions for both notes (default) and directions (explicit)
            int direction, note;
            Position(int x) : direction(x), note(x) {}
            void operator=(int x) { note=max(note,x); }
            operator int() const { return note; }
        };
        map<uint, Position> timeTrack; // Maps times to positions
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
        for(Sign sign: signs) {
            array<Chord>& beam = beams[sign.staff];
            if(beam && (
                        (sign.time%(timeSignature.beats*divisions) == (timeSignature.beats*divisions)/2 && sign.time>beam[0][0].time) || // Beam at half measure
                        (beam[0][0].time%divisions && sign.time>beam[0][0].time) || // Off beat (tail after complete chord)
                        (sign.type != Sign::Note && sign.type != Sign::Clef) ||
                        (sign.type == Sign::Note && sign.note.duration<Eighth && sign.time>beam[0][0].time))) {
                int clefStepSum = 0; for(const Chord& chord: beam) for(Sign sign: chord) clefStepSum += clefStep(sign.note.clef.clefSign, sign.note.step);
                bool tailUp = clefStepSum < (int(beam.size) * -4); // Average note height below mid staff

                if(beam.size==1) { // Draws single tail
                    Sign sign = tailUp ? beam[0].last() : beam[0].first();
                    int x = timeTrack.at(sign.time) + (tailUp?noteSize.x-2:0);
                    int yMin = Y(sign.note.clef, sign.staff, beam[0].first().note.step);
                    int yMax = Y(sign.note.clef, sign.staff, beam[0].last().note.step);
                    fills << Rect(int2(x, yMax-tailUp*tailLength), int2(x+tailWidth, yMin+(!tailUp)*tailLength));
                    int yTail = tailUp ? yMax-tailLength : yMin+tailLength;
                    /**/ if(sign.note.duration==Eighth) glyph(int2(x+tailWidth, yTail), tailUp?"flags.u3"_:"flags.d3"_);
                    else if(sign.note.duration==Sixteenth) glyph(int2(x+tailWidth, yTail), tailUp?"flags.u4"_:"flags.d4"_);
                } else if(beam.size==2) { // Draws slanted beam
                    int x[2], base[2], ttip[2];
                    for(uint i: range(2)) {
                        const Chord& chord = beam[i];
                        Sign sign = chord.first();
                        x[i] = timeTrack.at(sign.time)+(tailUp?noteSize.x-2:0);
                        base[i] = Y(sign.note.clef, sign.staff, (tailUp?chord.first():chord.last()).note.step);
                        ttip[i] = Y(sign.note.clef, sign.staff, (tailUp?chord.last():chord.first()).note.step)+(tailUp?-1:1)*(tailLength-noteSize.y/2);
                    }
                    /*int tip[2] = {(ttip[0]+ttip[1])/2 + clip(-lineInterval/2, (ttip[0]-ttip[1])/2, lineInterval/2),
                                  (ttip[0]+ttip[1])/2 + clip(-lineInterval/2, (ttip[1]-ttip[0])/2, lineInterval/2) };*/
                    int tiptip = tailUp?min(ttip[0],ttip[1]):max(ttip[0],ttip[1]);
                    int tip[2] = {tiptip + clip(-lineInterval, ttip[0]-tiptip, lineInterval),
                                  tiptip + clip(-lineInterval, ttip[1]-tiptip, lineInterval) };
                    for(uint i: range(2)) {
                        fills << Rect(int2(x[i],min(base[i],tip[i])),int2(x[i]+tailWidth, max(base[i],tip[i])));
                    }
                    Sign sign[2] = { tailUp?beam.first().last():beam.first().first(), tailUp?beam.last().last():beam.last().first()};
                    int2 p0 (timeTrack.at(sign[0].time)+(tailUp?noteSize.x-2:0), tip[0]+(tailUp?-1:1)*noteSize.y/2);
                    int2 p1 (timeTrack.at(sign[1].time)+(tailUp?noteSize.x-2:0)+tailWidth, tip[1]+(tailUp?-1:1)*noteSize.y/2);
                    parallelograms << Parallelogram{p0, p1, noteSize.y/2};
                } else { // Draws horizontal beam
                    int tailY = tailUp ? -1000 : 1000; //FIXME
                    const int shortLength = 2*tailLength/3;
                    if(tailUp) {
                        for(const Chord& chord: beam) for(Sign sign: chord) tailY = max(tailY, Y(sign)-tailLength);
                        for(const Chord& chord: beam) for(Sign sign: chord) tailY = min(tailY, Y(sign)-shortLength);
                    } else {
                        for(const Chord& chord: beam) for(Sign sign: chord) tailY = min(tailY, Y(sign)+tailLength);
                        for(const Chord& chord: beam) for(Sign sign: chord) tailY = max(tailY, Y(sign)+shortLength);
                    }
                    for(const Chord& chord: beam) for(Sign sign: chord) {
                        int x = timeTrack.at(sign.time) + (tailUp ? noteSize.x-2 : 0);
                        int y = Y(sign);
                        fills << Rect(int2(x,min(y, tailY)),int2(x+tailWidth, max(tailY, y)));
                    }
                    fills << Rect(int2(timeTrack.at(beam.first()[0].time) + (tailUp ? noteSize.x-2 : 0),            tailY-(tailUp?noteSize.y/2:0)),
                                  int2(timeTrack.at(beam.last()[0].time) + (tailUp ? noteSize.x-2 : 0) + tailWidth, tailY+(tailUp?0:noteSize.y/2)));
                }

                for(const Chord& chord: beam) {
                    Sign sign = tailUp ? chord.first() : chord.last();
                    int x = timeTrack.at(sign.time) + noteSize.x/2;
                    int y = Y(sign) + (tailUp?1:-1) * noteSize.y;
                    if(chord.first().note.staccato) glyph(int2(x,y),"scripts.staccato"_);
                    if(chord.first().note.tenuto) glyph(int2(x,y),"scripts.tenuto"_);
                    if(chord.first().note.accent) glyph(int2(x,y),"scripts.sforzato"_);
                 }
                beam.clear();
            }

            uint staff = sign.staff;
            if(timeTrack.contains(sign.time)) x = timeTrack.at(sign.time); // Synchronizes with previously laid signs
            else timeTrack.insert(sign.time, x); // Marks position for future signs

            /**/ if(sign.type == Sign::Note) {
                sign.note.clef = clefs.at(sign.staff);
                int step = clefStep(sign.note.clef.clefSign, sign.note.step);
                for(int s=2; s<=step; s+=2) { int y=staffY(staff, s); fills << int2(x-noteSize.x/2,y-1)+Rect(int2(2*noteSize.x,lineWidth)); }
                for(int s=-10; s>=step; s-=2) { int y=staffY(staff, s); fills << int2(x-noteSize.x/2,y-1)+Rect(int2(2*noteSize.x,lineWidth)); }
                int2 p = int2(x, Y(clefs, staff, sign.note.step));
                Duration duration = sign.note.duration;
                noteToBlit << blits.size;
                if(duration == Whole) x += 3*glyph(p, "noteheads.s0"_);
                else {
                    x += 3*glyph(p, duration==Half?"noteheads.s1"_:"noteheads.s2"_);
                    if(sign.note.accidental == Flat) glyph(p+int2(-noteSize.x,0),"accidentals.flat"_);
                    if(sign.note.accidental == Natural) glyph(p+int2(-noteSize.x,0),"accidentals.natural"_);
                    if(sign.note.accidental == Sharp) glyph(p+int2(-noteSize.x,0),"accidentals.sharp"_);
                    if(beam && beam.last().last().time == sign.time) beam.last().insertSorted(sign);
                    else beam << Chord(ref<Sign>({sign}));
                }
                if(sign.note.dot) glyph(p+int2(16,4),"dots.dot"_);
                array<Sign>& slur = slurs[sign.staff];
                if(slur) slur << sign;
                if(sign.note.slur) {
                    if(!slur) { /*if(x < int(target.width))*/ slur << sign; } // Starts new slur (only if visible)
                    else { // Stops
                        int clefStepSum = 0; for(Sign sign: slur) clefStepSum += clefStep(sign.note.clef.clefSign, sign.note.step);
                        int slurDown = clefStepSum < (int(slur.size) * -4) ? 1 : -1; // Average note height below mid staff

                        int y = slurDown>0 ? -1000 : 1000;
                        for(Sign sign: slur) y = slurDown>0 ? max(y, Y(sign)) : min(y, Y(sign));

                        vec2 p0 = vec2(timeTrack.at(slur.first().time), Y(clefs, slur.first().staff, slur.first().note.step)) + vec2(noteSize.x/2, slurDown*1*noteSize.y);
                        vec2 p1 = vec2(p) + vec2(noteSize.x/2, slurDown*2*noteSize.y);
                        vec2 k0 = vec2(p0.x, y) + vec2(0, slurDown*2*noteSize.y);
                        vec2 k0p = k0 + vec2(0, slurDown*noteSize.y/2);
                        vec2 k1 = vec2(p1.x, y) + vec2(0, slurDown*2*noteSize.y);
                        vec2 k1p = k1 + vec2(0, slurDown*noteSize.y/2);
                        cubics << Cubic(ref<vec2>({p0,k0,k1,p1,k1p,k0p}));
                        slur.clear();
                    } // TODO: continue
                }
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
                    x += noteSize.x;
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
                int px = glyphSize("pedal.Ped"_).x;
                if(sign.pedal.action == Change || sign.pedal.action == PedalStop) {
                    fills << Rect(int2(timeTrack.at(pedalStartTime)+px, y), int2(x, y+1));
                    fills << Rect(int2(x-1, y-lineInterval), int2(x, y));
                }
                if(sign.pedal.action == Start || sign.pedal.action == Change) pedalStartTime = sign.time;
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
                string word = ref<string>{"ppp"_,"pp"_,"p"_,"mp"_,"mf"_,"f"_,"ff"_,"fff"_}[uint(sign.dynamic.loudness)];
                float w = 0; for(char character: word.slice(0,word.size-1)) w += advance({character}); w += glyphSize({word.last()}).x;
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
                    if(staff==0) x=timeTrack.at(sign.time);
                    timeTrack.at(sign.time).direction = x;
                }
            }
            else if(sign.type==Sign::KeySignature) {
                keySignature = sign.keySignature;
                int fifths = keySignature.fifths;
                for(int i: range(abs(fifths))) {
                    int step = (fifths>0?4:6) + ((fifths>0 ? 4 : 3) * i +2)%7-2;
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
                array<uint> word = toUTF32("♩="_+dec(sign.metronome.perMinute));
                uint sx = x;
                for(uint code: word) {
                    uint16 index = textFont.index(code);
                    const Glyph& glyph = textFont.glyph(index);
                    blits << Blit{int2(sx, staffY(0, 16))+glyph.offset, share(glyph.image)};
                    sx += textFont.advance(index);
                }
            }

            if(timeTrack.contains(sign.time+sign.duration)) timeTrack.at(sign.time+sign.duration) = x; // Updates end position for future signs
            else timeTrack.insert(sign.time+sign.duration, x);
        }
    }

    // Render
    map<uint, vec3> colors; // Overrides color for Blit index
    int position = 0;
    int2 sizeHint() { return int2(-1, staffY(1,-16)); }
    void render(const Image& target) {
        int2 offset = int2(-position, (target.height - sizeHint().y)/2 + 4*lineInterval);
        // TODO: frustrum cull
        for(Rect r: fills) fill(target, offset+r);
        for(Parallelogram p: parallelograms) parallelogram(target, offset+p.min, offset+p.max, p.dy);
        for(uint i: range(blits.size)) { const Blit& b=blits[i]; blit(target, offset+b.position, b.image, colors.value(i, black)); }
        for(const Cubic& c: cubics) { buffer<vec2> points(c.size); for(uint i: range(c.size)) points[i]=vec2(offset)+c[i]; cubic(target, points); }
    }

    bool mouseEvent(int2, int2, Event, Button button) {
        const int delta = 3 * advance("noteheads.s2"_);
        if(button==WheelUp) { position = max(0, position-delta); return true; }
        if(button==WheelDown) { position = min(measures.last(), position+delta); return true; }
        return false;
    }
} test;
