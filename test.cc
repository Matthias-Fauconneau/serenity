#include "thread.h"
#include "xml.h"
#include "window.h"
#include "font.h"

struct MusicXML : Widget {
    enum ClefSign { Bass, Treble };
    enum Accidental { None, Flat /*♭*/, Sharp /*♯*/, Natural /*♮*/ };
    enum Duration { Invalid=-1, Eighth, Quarter, Half, Whole };

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
        int line; // 0 = C4
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

    array<Sign> signs;
    map<uint, vec3> colors;

    Window window {this, int2(1280, 384), "MusicXML"_};
    MusicXML() {
        Element root = parseXML(readFile("Storytime.xml"_, Folder("Scores"_,home())));
        map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0}; uint time = 0;
        root.xpath("score-partwise/part/measure"_, [&](const Element& m) {
            for(const Element& e: m.children) {
                if(e.name=="print"_) {}
                else if(e.name=="attributes"_) {
                    if(e("clef"_)) {
                        uint staff = fromInteger(e("clef"_)["number"_])-1;
                        ClefSign clefSign = ClefSign("FG"_.indexOf(e("clef"_)("sign"_).text()[0]));
                        //signs << Sign{time, staff, Sign::Clef, .clef={clefSign, 0}};
                        {Sign sign{time, staff, Sign::Clef, {}}; sign.clef={clefSign, 0}; signs << sign;};
                        clefs[staff] = {clefSign, 0};
                    }
                    if(e("key"_)) {
                        keySignature.fifths = fromInteger(e("key"_)("fifths"_).text());
                        //signs << Sign{time, 0, Sign::Key, .keySignature=keySignature};
                        {Sign sign{time, 0, Sign::KeySignature, {}}; sign.keySignature=keySignature; signs << sign;}
                    }
                    if(e("time"_)) {
                        timeSignature = {uint(fromInteger(e("time"_)("beats"_).text())), uint(fromInteger(e("time"_)("beat-type"_).text()))};
                        //signs << Sign{time, 0, Sign::TimeSignature, .timeSignature=timeSignature};
                        {Sign sign{time, 0, Sign::TimeSignature, {}}; sign.timeSignature=timeSignature; signs << sign;}
                    }
                }
                else if(e.name=="direction"_) {}
                else if(e.name=="note"_) {
                    uint staff = fromInteger(e("staff"_).text());
                    Duration duration = Duration(ref<string>{"eighth"_}.indexOf(e("type"_).text()));
                    if(e("rest"_)) {
                        {Sign sign{time, staff, Sign::Rest, {}}; sign.rest={duration}; signs << sign;}
                    } else {
                        assert_(e("pitch"_)("step"_).text(), e);
                        uint step = "CDEFGAB"_.indexOf(e("pitch"_)("step"_).text()[0]);
                        int octave = fromInteger(e("pitch"_)("octave"_).text());
                        int line = (octave-4) * 8 + step;
                        Accidental accidental = Accidental(ref<string>{""_,"flat"_,"sharp"_,"natural"_}.indexOf(e("accidental"_).text()));
                        //signs << Sign{time, staff, Sign::Note, .note={pitch, accidental, duration, e("grace"_)?true:false}};
                        {Sign sign{time, staff, Sign::Note, {}}; sign.note={line, accidental, duration, false /*dot*/, e("grace"_)?true:false}; signs << sign;};
                    }
                    if(e("duration"_)) time += fromInteger(e("duration"_).text());
                }
                else if(e.name=="backup"_) {
                    time -= fromInteger(e("duration"_).text());
                }
                else if(e.name=="forward"_) {
                    time += fromInteger(e("duration"_).text());
                }
                else if(e.name=="barline"_) {}
                else error(e);
            }
            signs << Sign{time, 0, Sign::Measure, {}};
        });
        window.background = Window::White;
        window.show();
    }

    const int halfLineInterval = 8, lineInterval = 2*halfLineInterval;
    const int staffMargin = 4*lineInterval;
    const int staffHeight = staffMargin + 4*lineInterval + staffMargin;
    const int staffCount = 2;

    int2 sizeHint() { return int2(-1, staffCount*staffHeight); }

    void render(const Image& target) {
        for(int staff: range(staffCount)) {
            for(int line: range(5)) {
                int y = staff * staffHeight + staffMargin + line*lineInterval;
                ::line(target, int2(0, y), int2(target.size().x, y));
            }
        }

        map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0};
        //map<int, Note> quavers; int tailMin=100, tailMax=-100; Duration minDuration=Invalid,maxDuration=Eighth;
        float x = 0;
        uint noteIndex = 0;
        for(Sign sign: signs) {
            auto position = [&](int line) -> int2 {
                const int trebleOffset = 10; // C4 position in intervals from top line
                const int bassOffset = -2; // C4 position in intervals from top line
                Clef clef = clefs[sign.staff];
                int clefOffset = clef.clefSign==Treble ? trebleOffset : clef.clefSign==Bass ? bassOffset : 0;
                return int2(x, sign.staff*staffHeight + staffMargin + (clefOffset - line) * halfLineInterval);
            };
            auto glyph = [&](int2 position, const string name, vec3 color=black) -> float {
                static Font font {File("emmentaler-20.otf"_, Folder("Scores"_,home())), 128};
                uint16 index = font.index(name);
                const Glyph& glyph = font.glyph(index);
                blit(target, position+glyph.offset, glyph.image, color);
                return font.advance(index);
            };
            if(sign.type == Sign::Clef) {
                Clef clef = clefs[sign.staff] = sign.clef;
                assert_(!clef.octave);
                if(clef.clefSign==Treble) x+=glyph(position(4),"clefs.G"_);
                if(clef.clefSign==Bass) x+=glyph(position(-4),"clefs.F"_);
            }

            if(sign.type==Sign::KeySignature) {
                keySignature = sign.keySignature;
                int fifths = keySignature.fifths;
                for(int i: range(abs(fifths))) {
                    int line = (fifths>0?4:6) + ((fifths>0 ? 4 : 3) * i +2)%8-2;
                    x+=glyph(position(line), fifths<0?"accidentals.flat"_:"accidentals.sharp"_);
                }
            }
            if(sign.type==Sign::TimeSignature) {
                timeSignature = sign.timeSignature;
                static constexpr string numbers[10] = {"zero"_,"one"_,"two"_,"three"_,"four"_,"five"_,"six"_,"seven"_,"eight"_,"nine"_};
                glyph(position(3),numbers[timeSignature.beats]);
                x+=glyph(position(7),numbers[timeSignature.beatUnit]);
            }

            if(sign.type == Sign::Note) {
                //for(int i=-2;i>=h;i-=2) drawLedger(s, t, i);
                //for(int i=10;i<=h;i+=2) drawLedger(s, t, i);
                int2 p = position(sign.note.line);
                Duration duration = sign.note.duration;
                if(duration == Whole) x+=glyph(p,"noteheads.s0"_, colors.value(noteIndex,black));
                else {
                    x+=glyph(p, duration==Half?"noteheads.s1"_:"noteheads.s2"_, colors.value(noteIndex,black));
                    if(sign.note.accidental == Flat) glyph(p+int2(-12,0),"accidentals.flat"_);
                    if(sign.note.accidental == Natural) glyph(p+int2(-12,0),"accidentals.natural"_);
                    if(sign.note.accidental == Sharp) glyph(p+int2(-12,0),"accidentals.sharp"_);
                    /*if(duration<=Eighth) quavers[sign.staff] << note;
                    else {
                        tailMin = min(tailMin,y), tailMax = max(tailMax, y);
                        minDuration = min(minDuration,duration), maxDuration=max(maxDuration, duration);
                    }*/
                }
                noteIndex++;
                if(sign.note.dot) glyph(p+int2(16,4),"dots.dot"_);
            }
            if(sign.type == Sign::Measure) {
                // Align tails length
                /*if(tailMin<=tailMax) {
                    bool tailUp = !s;
                    int x = page(s,t,0).x + (tailUp ? 12 : 0);
                    line(vec2(x+0.5, page(s,t,tailMax).y+(tailUp?0:32)),vec2(x+0.5, page(s,t,tailMin).y+(tailUp?-32:0)),2);
                    //assert(minDuration==maxDuration,minDuration,maxDuration);
                    //if(minDuration!=maxDuration) Text(String("!"_)).render(int2(x,page(s,t,tailMin).y));
                }*/
                // Links quaver tails (FIXME: on beats)
                /*for(int s: range(2)) {
                    Clef clef = (Clef)s;
                    bool tailUp=true; int dx = tailUp ? 12 : 0; uint slurY=tailUp?-1:0;
                    uint begin=0;
                    for(uint i: range(quavers[s].size)) {
                        MidiNote note = quavers[s][i];
                        int2 position = page(s, note.start, staffY(clef, note.key));
                        if(tailUp) slurY=min<uint>(slurY,position.y);
                        else slurY=max<uint>(slurY,position.y);
                        uint duration=note.duration;
                        if(i+1>=quavers[s].size || quavers[s][i+1].duration<duration || (quavers[s][i+1].start != note.start && quavers[s][i+1].start != note.start+duration)) {
                            ref<MidiNote> linked = quavers[s].slice(begin,i+1-begin);
                            if(linked.size==1) slurY+=tailUp?-32:32; else slurY+=tailUp?-24:24;
                            int2 lastPosition=0;
                            for(MidiNote note : linked) {
                                int2 position = page(s,note.start,staffY(clef, note.key));
                                int x = position.x + dx;
                                line(vec2(x+0.5,position.y),vec2(x+0.5,slurY),2);
                                if(linked.size==1) { // draws single tail
                                    int x = position.x + dx;
                                    if(note.duration==1) glyph(int2(x+1,slurY),tailUp?"flags.u4"_:"flags.d4"_);
                                    else if(note.duration==2) glyph(int2(x+1,slurY),tailUp?"flags.u3"_:"flags.d3"_);
                                } else if(lastPosition){ // draws horizontal tail links
                                    if(note.duration==1) {
                                        line(vec2(lastPosition.x+dx,slurY+(tailUp?7:-7)+0.5),vec2(position.x+dx,slurY+(tailUp?7:-7)+0.5),2);
                                        line(vec2(lastPosition.x+dx,slurY+(tailUp?9:-9)+0.5),vec2(position.x+dx,slurY+(tailUp?9:-9)+0.5),2);
                                    }
                                    line(vec2(lastPosition.x+dx,slurY+0.5),vec2(position.x+dx,slurY+0.5),2);
                                    line(vec2(lastPosition.x+dx,slurY+(tailUp?2:-2)+0.5),vec2(position.x+dx,slurY+(tailUp?2:-2)+0.5),2);
                                }
                                lastPosition=position;
                            }
                            begin=i+1;
                            slurY=tailUp?-1:0;
                        }
                    }
                    quavers[s].clear();
                  */
                //tailMin=100, tailMax=-100;
                //line(page(1,t,0)-int2(8,0),page(0,t,8)-int2(8,0));
            }
        }
    }
} test;
