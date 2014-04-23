#include "thread.h"
#include "xml.h"
#include "window.h"
#include "font.h"

struct MusicXML : Widget {
    enum ClefSign { Bass, Treble };
    enum Accidental { None, Flat /*♭*/, Sharp /*♯*/, Natural /*♮*/ };
    enum Duration { Invalid=-1, Whole, Half, Quarter, Eighth, Sixteenth };

    struct Clef {
        ClefSign clefSign;
        int octave;
    };
    struct KeySignature {
        int fifths; // Index on the fifths circle
    };
    struct TimeSignature {
        uint beats, beatUnit;
    };
    struct Note {
        int step; // 0 = C4
        Accidental accidental;
        Duration duration;
        bool dot;
        bool grace;
    };
    struct Rest {
        Duration duration;
    };

    struct Sign {
        uint time; // Absolute time offset
        uint duration;
        uint staff; // Staff index
        enum { Note, Rest, Clef, KeySignature, TimeSignature, Measure } type;
        union {
            struct Note note;
            struct Rest rest;
            struct Clef clef;
            struct KeySignature keySignature;
            struct TimeSignature timeSignature;
        };
    };

    uint divisions = 256;
    array<Sign> signs;
    map<uint, vec3> colors;

    Window window {this, int2(1280, 384), "MusicXML"_};
    MusicXML() {
        Element root = parseXML(readFile("Storytime.xml"_, Folder("Scores"_,home())));
        map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0}; uint time = 0, nextTime = 0;
        root.xpath("score-partwise/part/measure"_, [&](const Element& m) {
            for(const Element& e: m.children) {
                if(!(e.name=="note"_ && e("chord"_))) time = nextTime; // Reverts previous advance

                if(e.name=="print"_) {}
                else if(e.name=="attributes"_) {
                    if(e("divisions"_)) divisions = fromInteger(e("divisions"_).text());
                    e.xpath("clef"_, [&](const Element& clef) {
                        uint staff = fromInteger(clef["number"_])-1;
                        ClefSign clefSign = ClefSign("FG"_.indexOf(clef("sign"_).text()[0]));
                        //signs << Sign{time, 0, staff, Sign::Clef, .clef={clefSign, 0}};
                        {Sign sign{time, 0, staff, Sign::Clef, {}}; sign.clef={clefSign, 0}; signs << sign;};
                        clefs[staff] = {clefSign, 0};
                    });
                    if(e("key"_)) {
                        keySignature.fifths = fromInteger(e("key"_)("fifths"_).text());
                        //signs << Sign{time, 0, 0, Sign::Key, .keySignature=keySignature};
                        {Sign sign{time, 0, 0, Sign::KeySignature, {}}; sign.keySignature=keySignature; signs << sign;}
                    }
                    if(e("time"_)) {
                        timeSignature = {uint(fromInteger(e("time"_)("beats"_).text())), uint(fromInteger(e("time"_)("beat-type"_).text()))};
                        //signs << Sign{time, 0, 0, Sign::TimeSignature, .timeSignature=timeSignature};
                        {Sign sign{time, 0, 0, Sign::TimeSignature, {}}; sign.timeSignature=timeSignature; signs << sign;}
                    }
                }
                else if(e.name=="direction"_) {}
                else if(e.name=="note"_) {
                    uint staff = fromInteger(e("staff"_).text())-1;
                    Duration type = Duration(ref<string>{"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_}.indexOf(e("type"_).text()));
                    assert_(int(type)>=0, e);
                    uint duration = e("duration"_) ? fromInteger(e("duration"_).text()) : 0;
                    if(e("rest"_)) {
                        {Sign sign{time, duration, staff, Sign::Rest, {}}; sign.rest={type}; signs << sign;}
                    } else {
                        assert_(e("pitch"_)("step"_).text(), e);
                        uint octaveStep = "CDEFGAB"_.indexOf(e("pitch"_)("step"_).text()[0]);
                        int octave = fromInteger(e("pitch"_)("octave"_).text());
                        int step = (octave-4) * 7 + octaveStep;
                        Accidental accidental = Accidental(ref<string>{""_,"flat"_,"sharp"_,"natural"_}.indexOf(e("accidental"_).text()));
                        //signs << Sign{time, staff, Sign::Note, .note={pitch, accidental, type, e("grace"_)?true:false}};
                        {Sign sign{time, duration, staff, Sign::Note, {}}; sign.note={step, accidental, type, false /*dot*/, e("grace"_)?true:false}; signs << sign;};
                    }
                    nextTime = time+duration;
                }
                else if(e.name=="backup"_) {
                    time -= fromInteger(e("duration"_).text());
                    nextTime = time;
                }
                else if(e.name=="forward"_) {
                    time += fromInteger(e("duration"_).text());
                    nextTime = time;
                }
                else if(e.name=="barline"_) {}
                else error(e);
            }
            time=nextTime;
            signs << Sign{time, 0, 0, Sign::Measure, {}};
        });
        window.background = Window::White;
        window.actions[Escape] = []{exit();};
        window.show();
    }

    const int halfLineInterval = 8, lineInterval = 2*halfLineInterval;
    const int staffMargin = 4*lineInterval;
    const int staffHeight = staffMargin + 4*lineInterval + staffMargin;
    const int staffCount = 2;

    int2 sizeHint() { return int2(-1, staffCount*staffHeight); }

    Font font {File("emmentaler-20.otf"_, Folder("Scores"_,home())), 24*halfLineInterval};
    vec2 glyphSize(const string name) { return font.size(font.index(name)); }
    int2 noteSize = int2(round(glyphSize("noteheads.s0"_)));
    int tailWidth = 2, tailLength = 4*noteSize.y;
    float glyph(const Image& target, int2 position, const string name, vec3 color=black) {
        uint16 index = font.index(name);
        const Glyph& glyph = font.glyph(index);
        blit(target, position+glyph.offset, glyph.image, color);
        return font.advance(index);
    }
    int clefStep(ClefSign clefSign, int step) { return step - (clefSign==Treble ? 10 : -2); } // Translates C4 step to clef relative step
    int staffY(uint staff, int clefStep) { return staff*staffHeight + staffMargin - clefStep * halfLineInterval; } // Clef independent
    int Y(const map<uint, Clef>& clefs, uint staff, int step) { return staffY(staff, clefStep(clefs.at(staff).clefSign, step)); }; // Clef dependent

    void render(const Image& target) {

        uint x = noteSize.x/2;

        for(int staff: range(staffCount)) {
            for(int line: range(5)) {
                int y = staff * staffHeight + staffMargin + line*lineInterval;
                ::line(target, int2(x, y), int2(target.size().x, y));
            }
        }
        //TODO: brace
        fill(target, Rect(int2(x-1,staffMargin),int2(x+1,staffMargin+staffHeight+4*lineInterval))); // System

        map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0};
        array<Sign> eighths;
        map<uint, uint> timeTrack; // Maps times to positions
        uint noteIndex = 0;
        for(Sign sign: signs) {
            if(eighths && ((sign.time%divisions == 0 && eighths.size==1) || sign.type != Sign::Note || sign.note.duration<Eighth || sign.staff != eighths.last().staff)) {
                bool tailUp = false;
                if(eighths.size==1) { // Draws single tail
                    Sign sign = eighths.last();
                    Note note = sign.note;
                    int x = timeTrack[sign.time];
                    int y = Y(clefs, sign.staff, note.step);
                    fill(target, int2(x,y)+Rect(int2(tailWidth, tailLength)));
                    /**/ if(note.duration==Eighth) glyph(target, int2(x+tailWidth, y+tailLength), tailUp?"flags.u3"_:"flags.d3"_);
                    else if(note.duration==Sixteenth) glyph(target, int2(x+tailWidth, y+tailLength), tailUp?"flags.u4"_:"flags.d4"_);
                } else if(eighths.size==2) { // Draws slanted beam
                    for(Sign sign: eighths) {
                        int x = timeTrack[sign.time];
                        int y = Y(clefs, sign.staff, sign.note.step);
                        fill(target, Rect(int2(x,y),int2(x+tailWidth, y+tailLength)));
                    }
                    for(int dy: range(noteSize.y*2)) { // FIXME: Rasterize quad
                        line(target, vec2(timeTrack[eighths.first().time], Y(clefs, eighths.first().staff, eighths.first().note.step)+tailLength+dy/4),
                                vec2(timeTrack[eighths.last().time]+1, Y(clefs, eighths.last().staff, eighths.last().note.step)+tailLength+dy/4));
                    }
                } else { // Draws horizontal beam
                    int tailY = target.size().y;
                    for(Sign sign: eighths) tailY = min(tailY, Y(clefs, sign.staff, sign.note.step)+tailLength);
                    for(Sign sign: eighths) tailY = max(tailY, Y(clefs, sign.staff, sign.note.step)+tailLength/2);
                    for(Sign sign: eighths) {
                        int x = timeTrack[sign.time];
                        int y = Y(clefs, sign.staff, sign.note.step);
                        fill(target, Rect(int2(x,y),int2(x+tailWidth, tailY)));
                    }
                    fill(target, Rect(int2(timeTrack[eighths.first().time],tailY),int2(timeTrack[eighths.last().time]+tailWidth, tailY+noteSize.y/2)));
                }
                eighths.clear();
            }

            uint staff = sign.staff;
            if(timeTrack.contains(sign.time)) x = timeTrack.at(sign.time); // Synchronizes with previously laid signs
            else timeTrack.insert(sign.time, x); // Marks position for future signs

            /**/ if(sign.type == Sign::Clef) {
                string change = clefs.contains(sign.staff)?""_:"_change"_;
                Clef clef = clefs[sign.staff] = sign.clef;
                assert_(!clef.octave);
                x += noteSize.x/2;
                if(clef.clefSign==Treble) x += glyph(target, int2(x,Y(clefs,staff,4)), "clefs.G"_+change);
                if(clef.clefSign==Bass) x += glyph(target, int2(x,Y(clefs,staff,-4)),"clefs.F"_+change);
                x += noteSize.x/2;
                if(staff==0) x=timeTrack.at(sign.time);
            }
            else if(sign.type==Sign::KeySignature) {
                keySignature = sign.keySignature;
                int fifths = keySignature.fifths;
                for(int i: range(abs(fifths))) {
                    int step = (fifths>0?4:6) + ((fifths>0 ? 4 : 3) * i +2)%7-2;
                    glyph(target, int2(x, Y(clefs,0,step)), fifths<0?"accidentals.flat"_:"accidentals.sharp"_);
                    x += glyph(target, int2(x, Y(clefs,1,step - (clefs[1u].clefSign==Bass ? 14 : 0))), fifths<0?"accidentals.flat"_:"accidentals.sharp"_);
                }
                x += noteSize.x/2;
            }
            else if(sign.type==Sign::TimeSignature) {
                timeSignature = sign.timeSignature;
                static constexpr string numbers[10] = {"zero"_,"one"_,"two"_,"three"_,"four"_,"five"_,"six"_,"seven"_,"eight"_,"nine"_};
                glyph(target, int2(x, staffY(0, -4)),numbers[timeSignature.beats]);
                glyph(target, int2(x, staffY(1, -4)),numbers[timeSignature.beats]);
                glyph(target, int2(x, staffY(0, -8)),numbers[timeSignature.beatUnit]);
                x += 2*glyph(target, int2(x, staffY(1, -8)),numbers[timeSignature.beatUnit]);
            }
            else if(sign.type == Sign::Note) {
                int step = clefStep(clefs[staff].clefSign, sign.note.step);
                for(int s=2; s<=step; s+=2) { int y=staffY(staff, s); ::fill(target, int2(x-noteSize.x/6,y-1)+Rect(int2(noteSize.x,2))); }
                for(int s=-10; s>=step; s-=2) { int y=staffY(staff, s); ::fill(target, int2(x-noteSize.x/6,y-1)+Rect(int2(noteSize.x,2))); }
                int2 p = int2(x, Y(clefs, staff, sign.note.step));
                Duration duration = sign.note.duration;
                if(duration == Whole) x += 3*glyph(target, p, "noteheads.s0"_, colors.value(noteIndex,black));
                else {
                    x += 3*glyph(target, p, duration==Half?"noteheads.s1"_:"noteheads.s2"_, colors.value(noteIndex,black));
                    if(sign.note.accidental == Flat) glyph(target, p+int2(-noteSize.x*2/3,0),"accidentals.flat"_);
                    if(sign.note.accidental == Natural) glyph(target, p+int2(-noteSize.x*2/3,0),"accidentals.natural"_);
                    if(sign.note.accidental == Sharp) glyph(target, p+int2(-noteSize.x*2/3,0),"accidentals.sharp"_);
                    if(duration<=Eighth) eighths << sign;
                    else fill(target, p+Rect(int2(1,tailLength)));
                }
                noteIndex++;
                if(sign.note.dot) glyph(target, p+int2(16,4),"dots.dot"_);
            }
            else if(sign.type == Sign::Rest) {
                int2 p = int2(x, staffY(staff, -4));
                if(sign.rest.duration == Whole) x+= 3*glyph(target, p, "rests.0"_);
                else if(sign.rest.duration == Half) x+= 3*glyph(target, p, "rests.1"_);
                else if(sign.rest.duration == Quarter) x+= 3*glyph(target, p, "rests.2"_);
                else if(sign.rest.duration == Eighth) x+= 3*glyph(target, p, "rests.3"_);
                else if(sign.rest.duration == Sixteenth) x+= 3*glyph(target, p, "rests.4"_);
                else error(int(sign.rest.duration));
            }
            else if(sign.type == Sign::Measure) {
                fill(target, Rect(int2(x-1,staffY(0,0)),int2(x+1,staffY(1,-8))));
                x += noteSize.x;
            }
            timeTrack[sign.time+sign.duration] = max(x, timeTrack[sign.time+sign.duration]); // Marks position for future signs
        }
    }
} test;
