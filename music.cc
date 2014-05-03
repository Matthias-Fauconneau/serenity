#include "thread.h"
#include "xml.h"
#include "window.h"
#include "font.h"
#include "utf8.h"

// -> graphics.h

// 8bit signed integer (for edge flags)
struct Image8 {
    Image8(uint width, uint height) : width(width), height(height) {
        assert(width); assert(height);
        data = ::buffer<int8>(height*width);
        data.clear(0);
    }
    int8& operator()(uint x, uint y) {assert(x<width && y<height, x, y, width, height); return data[y*width+x]; }
    buffer<int8> data;
    uint width, height;
};

// FIXME: Coverage integration
static int lastStepY; //dont flag first/last point twice but cancel on direction changes
inline void line(Image8& raster, int2 p0, int2 p1) {
    int x0=p0.x, y0=p0.y, x1=p1.x, y1=p1.y;
    int dx = abs(x1-x0);
    int dy = abs(y1-y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx-dy;
    if(sy!=lastStepY) raster(x0,y0) -= sy;
    for(;;) {
        if(x0 == x1 && y0 == y1) break;
        int e2 = 2*err;
        if(e2 > -dy) { err -= dy, x0 += sx; }
        if(e2 < dx) { err += dx, y0 += sy; raster(x0,y0) -= sy; } //only raster at y step
    }
    lastStepY=sy;
}

inline vec2 cubic(vec2 A,vec2 B,vec2 C,vec2 D,float t) { return ((1-t)*(1-t)*(1-t))*A + (3*(1-t)*(1-t)*t)*B + (3*(1-t)*t*t)*C + (t*t*t)*D; }
inline void cubic(Image8& raster, vec2 A, vec2 B, vec2 C, vec2 D) {
    const int N = 12;
    int2 a = int2(round(A));
    for(int i : range(1,N+1)) {
        int2 b = int2(round(cubic(A,B,C,D,float(i)/N)));
        line(raster, a, b);
        a=b;
    }
}

// Renders cubic spline (two control points between each end point)
void cubic(const Image& target, const ref<vec2>& points, vec3 color=black, float alpha=1) {
    vec2 pMin = vec2(target.size()), pMax = 0;
    for(vec2 p: points) pMin = ::min(pMin, p), pMax = ::max(pMax, p);
    pMin = floor(pMin), pMax = ceil(pMax);
    int2 iMin = int2(pMin), iMax = int2(pMax);
    int2 size = iMax-iMin;
    if(!size) return;
    const uint oversample = 8;
    Image8 raster(oversample*size.x+1,oversample*size.y+1);
    for(uint i=0;i<points.size; i+=3) {
        cubic(raster, float(oversample)*(points[i]-pMin), float(oversample)*(points[(i+1)%points.size]-pMin), float(oversample)*(points[(i+2)%points.size]-pMin),
                float(oversample)*(points[(i+3)%points.size]-pMin));
    }
    iMin = max(int2(0),iMin), iMax = min(target.size(), iMax);
    for(uint y: range(iMin.y, iMax.y)) {
        int acc[oversample]={};
        for(uint x: range(iMin.x, iMax.x)) { //Supersampled rasterization
            int coverage = 0;
            for(uint j: range(oversample)) for(int i: range(oversample)) {
                acc[j] += raster((x-iMin.x)*oversample+i, (y-iMin.y)*oversample+j);
                //assert_(acc[j]>=0, acc[j]);
                coverage += acc[j]!=0;
            }
            if(coverage) blend(target, x, y, color, float(coverage)/sq(oversample)*alpha);
        }
    }
}

enum ClefSign { Bass, Treble };
enum Accidental { None, Flat /*♭*/, Sharp /*♯*/, Natural /*♮*/ };
enum Duration { Whole, Half, Quarter, Eighth, Sixteenth };
enum Loudness { ppp, pp, p, mp, mf, f, ff, fff };

struct Clef {
    ClefSign clefSign;
    int octave;
};
struct Note {
    Clef clef;
    int step; // 0 = C4
    Accidental accidental;
    Duration duration;
    bool dot:1;
    bool grace:1;
    bool staccato:1;
    bool slur:1; // toggle
};
struct Rest {
    Duration duration;
};
struct Dynamic {
    Loudness loudness;
};
struct KeySignature {
    int fifths; // Index on the fifths circle
};
struct TimeSignature {
    uint beats, beatUnit;
};
struct Metronome {
    Duration beatUnit;
    uint perMinute;
};

struct Sign {
    uint time; // Absolute time offset
    uint duration;
    uint staff; // Staff index
    enum { Note, Rest, Measure, Dynamic, Clef, KeySignature, TimeSignature, Metronome } type;
    union {
        struct Note note;
        struct Rest rest;
        struct Clef clef;
        struct KeySignature keySignature;
        struct TimeSignature timeSignature;
        struct Metronome metronome;
        struct Dynamic dynamic;
    };
};
bool operator <=(const Sign& a, const Sign& b) { return a.time <= b.time; }

struct MusicXML : Widget {


    uint divisions = 256;
    array<Sign> signs;
    map<uint, vec3> colors;

    Window window {this, int2(0, 384), "MusicXML"_};
    MusicXML() {
        Element root = parseXML(readFile("Storytime.xml"_, Folder("Scores"_,home())));
        map<uint, Clef> clefs; map<uint, bool> slurs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0}; uint time = 0, nextTime = 0;
        root.xpath("score-partwise/part/measure"_, [&](const Element& m) {
            for(const Element& e: m.children) {
                if(!(e.name=="note"_ && e("chord"_))) time = nextTime; // Reverts previous advance

                if(e.name=="note"_) {
                    uint staff = fromInteger(e("staff"_).text())-1;
                    Duration type = Duration(ref<string>{"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_}.indexOf(e("type"_).text()));
                    assert_(int(type)>=0, e);
                    uint duration = e("duration"_) ? fromInteger(e("duration"_).text()) : 0;
                    if(e("rest"_)) {
                        {Sign sign{time, duration, staff, Sign::Rest, {}}; sign.rest={type}; signs.insertSorted(sign);}
                    } else {
                        assert_(e("pitch"_)("step"_).text(), e);
                        uint octaveStep = "CDEFGAB"_.indexOf(e("pitch"_)("step"_).text()[0]);
                        int octave = fromInteger(e("pitch"_)("octave"_).text());
                        int step = (octave-4) * 7 + octaveStep;
                        Accidental accidental = Accidental(ref<string>{""_,"flat"_,"sharp"_,"natural"_}.indexOf(e("accidental"_).text()));
                        if(e("notations"_)("slur"_)) {
                            if(slurs[staff]) assert_(e("notations"_)("slur"_).attribute("type"_)=="stop"_);
                            else assert_(e("notations"_)("slur"_).attribute("type"_)=="start"_, e("notations"_)("slur"_).attribute("type"_), e);
                            slurs[staff] = !slurs[staff];
                        }
                        //signs << Sign{time, staff, Sign::Note, .note={pitch, accidental, type, e("grace"_)?true:false}};
                        {Sign sign{time, duration, staff, Sign::Note, {}};
                            sign.note={clefs.at(staff), step, accidental, type,
                                       false /*dot*/,
                                       e("grace"_)?true:false,
                                       e("notations"_)("articulations"_)("staccato"_)?true:false,
                                       e("notations"_)("slur"_)?true:false
                                      };
                            signs.insertSorted(sign);};
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
                else if(e.name=="direction"_) {
                    const Element& d = e("direction-type"_);
                    if(d("dynamics"_)) {
                        Loudness loudness = Loudness(ref<string>{"ppp"_,"pp"_,"p"_,"mp"_,"mf"_,"f"_,"ff"_,"fff"_}.indexOf(d("dynamics"_).children.first()->name));
                        {Sign sign{time, 0, 0, Sign::Dynamic, {}}; sign.dynamic={loudness}; signs << sign;}
                    }
                    else if(d("metronome"_)) {
                        Duration beatUnit = Duration(ref<string>{"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_}.indexOf(d("metronome"_)("beat-unit"_).text()));
                        uint perMinute = fromInteger(d("metronome"_)("per-minute"_).text());
                        {Sign sign{time, 0, 0, Sign::Metronome, {}}; sign.metronome={beatUnit, perMinute}; signs << sign;}
                    }
                    else if(d("pedal"_)) {}
                    else if(d("wedge"_)) {}
                    else if(d("octave-shift"_)) {}
                    else if(d("other-direction"_)) {}
                    else error(e);
                }
                else if(e.name=="attributes"_) {
                    if(e("divisions"_)) divisions = fromInteger(e("divisions"_).text());
                    e.xpath("clef"_, [&](const Element& clef) {
                        uint staff = fromInteger(clef["number"_])-1;
                        ClefSign clefSign = ClefSign("FG"_.indexOf(clef("sign"_).text()[0]));
                        {Sign sign{time, 0, staff, Sign::Clef, {}}; sign.clef={clefSign, 0}; signs << sign;};
                        clefs[staff] = {clefSign, 0};
                    });
                    if(e("key"_)) {
                        keySignature.fifths = fromInteger(e("key"_)("fifths"_).text());
                        {Sign sign{time, 0, 0, Sign::KeySignature, {}}; sign.keySignature=keySignature; signs.insertSorted(sign); }
                    }
                    if(e("time"_)) {
                        timeSignature = {uint(fromInteger(e("time"_)("beats"_).text())), uint(fromInteger(e("time"_)("beat-type"_).text()))};
                        {Sign sign{time, 0, 0, Sign::TimeSignature, {}}; sign.timeSignature=timeSignature; signs << sign;}
                    }
                }
                else if(e.name=="barline"_) {}
                else if(e.name=="print"_) {}
                else error(e);
            }
            time=nextTime;
            signs.insertSorted(Sign{time, 0, 0, Sign::Measure, {}});
            signs.insertSorted(Sign{time, 0, 1, Sign::Measure, {}});
        });
        window.background = Window::White;
        window.actions[Escape] = []{exit();};
        window.show();
    }

    const int halfLineInterval = 4, lineInterval = 2*halfLineInterval;
    const int staffMargin = 6*lineInterval;
    const int staffHeight = staffMargin + 4*lineInterval + staffMargin;
    const int staffCount = 2;

    Font font {File("emmentaler-26.otf"_, Folder("Scores"_,home())), 4*lineInterval};
    vec2 glyphSize(const string name) { return font.size(font.index(name)); }

    int2 noteSize = int2(round(glyphSize("noteheads.s2"_)));
    const int lineWidth = 1;
    const int tailWidth = lineWidth /*halfLineInterval/4*/, tailLength = 4*noteSize.y;

    float advance(const string name) { return font.advance(font.index(name)); }
    float glyph(const Image& target, int2 position, const string name, vec3 color=black) {
        uint16 index = font.index(name);
        const Glyph& glyph = font.glyph(index);
        blit(target, position+glyph.offset, glyph.image, color);
        return font.advance(index);
    }

    int clefStep(ClefSign clefSign, int step) { return step - (clefSign==Treble ? 10 : -2); } // Translates C4 step to top line step using clef
    int staffY(uint staff, int clefStep) { return staff*staffHeight + staffMargin - clefStep * halfLineInterval; } // Clef independent
    int Y(const map<uint, Clef>& clefs, uint staff, int step) { return staffY(staff, clefStep(clefs.at(staff).clefSign, step)); }; // Clef dependent

    Font textFont{File("FreeSerifBold.ttf"_,Folder("Scores"_,home())), 6*halfLineInterval};

    int position = 0;
    int2 sizeHint() { return int2(-1, staffCount*staffHeight); }

    void render(const Image& target) {
        int y0 = (target.height - sizeHint().y)/2;
        int x = noteSize.x - position * 3 * advance("noteheads.s2"_);

        for(int staff: range(staffCount)) {
            for(int line: range(5)) {
                int y = y0 + staff * staffHeight + staffMargin + line*lineInterval;
                fill(target, Rect(int2(x, y), int2(target.size().x, y+lineWidth)));
            }
        }
        //TODO: brace
        fill(target, Rect(int2(x-1, y0+staffMargin), int2(x+1, y0+staffMargin+staffHeight+4*lineInterval))); // System

        map<uint, Clef> clefs; KeySignature keySignature={0}; TimeSignature timeSignature={0,0};
        array<Sign> beams[2]; // Signs belonging to current beam (per staff)
        array<Sign> slurs[2]; // Signs belonging to current slur (per staff)
        struct Position { // Holds positions for both notes (default) and directions (explicit)
            int direction, note;
            Position(int x) : direction(x), note(x) {}
            void operator=(int x) { note=max(note,x); }
            operator int() const { return note; }
        };
        map<uint, Position> timeTrack; // Maps times to positions
        uint noteIndex = 0;
        for(Sign sign: signs) {
            array<Sign>& beam = beams[sign.staff];
            if(beam && (
                        sign.time%(timeSignature.beats*divisions) == (timeSignature.beats*divisions)/2 ||
                        beam.first().time%divisions ||
                        sign.type != Sign::Note ||
                        sign.note.duration<Eighth)) {
                int clefStepSum = 0; for(Sign sign: beam) clefStepSum += clefStep(sign.note.clef.clefSign, sign.note.step);
                bool tailUp = clefStepSum < (int(beam.size) * -4); // Average note height below mid staff

                if(beam.size==1) { // Draws single tail
                    Sign sign = beam.last();
                    Note note = sign.note;
                    int x = timeTrack.at(sign.time);
                    int y = y0+Y(clefs, sign.staff, note.step);
                    fill(target, int2(x+(tailUp?noteSize.x-2:0),y-tailUp*tailLength)+Rect(int2(tailWidth, tailLength)));
                    /**/ if(note.duration==Eighth) glyph(target, int2(x+(tailUp?noteSize.x-2:0)+tailWidth, y+(tailUp?-1:1)*tailLength), tailUp?"flags.u3"_:"flags.d3"_);
                    else if(note.duration==Sixteenth) glyph(target, int2(x+(tailUp?noteSize.x-2:0)+tailWidth, y+(tailUp?-1:1)*tailLength), tailUp?"flags.u4"_:"flags.d4"_);
                } else if(beam.size==2) { // Draws slanted beam
                    for(Sign sign: beam) {
                        int x = timeTrack.at(sign.time)+(tailUp?noteSize.x-2:0);
                        int y = y0+Y(clefs, sign.staff, sign.note.step);
                        int y1 = y+(tailUp?-1:1)*(tailLength-noteSize.y);
                        fill(target, Rect(int2(x,min(y,y1)),int2(x+tailWidth, max(y,y1))));
                    }
                    int2 p0 (timeTrack.at(beam.first().time)+(tailUp?noteSize.x-2:0),
                             y0+Y(clefs, beam.first().staff, beam.first().note.step)+(tailUp?-1:1)*(tailLength-noteSize.y));
                    int2 p1 (timeTrack.at(beam.last ().time)+(tailUp?noteSize.x-2:0)+tailWidth,
                             y0+Y(clefs, beam.last ().staff, beam.last ().note.step)+(tailUp?-1:1)*(tailLength-noteSize.y));
                    parallelogram(target, p0, p1, noteSize.y/2);
                } else { // Draws horizontal beam
                    int tailY = tailUp ? 0 : target.size().y;
                    if(tailUp) {
                        for(Sign sign: beam) tailY = max(tailY, y0+Y(clefs, sign.staff, sign.note.step)+(tailUp?-1:1)*(tailLength/(tailUp?1:2)));
                        for(Sign sign: beam) tailY = min(tailY, y0+Y(clefs, sign.staff, sign.note.step)+(tailUp?-1:1)*(tailLength/(tailUp?2:1)));
                    } else {
                        for(Sign sign: beam) tailY = min(tailY, y0+Y(clefs, sign.staff, sign.note.step)+(tailUp?-1:1)*(tailLength/(tailUp?2:1)));
                        for(Sign sign: beam) tailY = max(tailY, y0+Y(clefs, sign.staff, sign.note.step)+(tailUp?-1:1)*(tailLength/(tailUp?1:2)));
                    }
                    for(Sign sign: beam) {
                        int x = timeTrack.at(sign.time) + (tailUp ? noteSize.x-2 : 0);
                        int y = y0+Y(clefs, sign.staff, sign.note.step);
                        fill(target, Rect(int2(x,min(y, tailY)),int2(x+tailWidth, max(tailY, y))));
                    }
                    fill(target, Rect(int2(timeTrack.at(beam.first().time) + (tailUp ? noteSize.x-2 : 0),            tailY-(tailUp?noteSize.y/2:0)),
                                      int2(timeTrack.at(beam.last().time) + (tailUp ? noteSize.x-2 : 0) + tailWidth, tailY+(tailUp?0:noteSize.y/2))));
                }
                beam.clear();
            }

            uint staff = sign.staff;
            if(timeTrack.contains(sign.time)) x = timeTrack.at(sign.time); // Synchronizes with previously laid signs
            else timeTrack.insert(sign.time, x); // Marks position for future signs

            /**/ if(sign.type == Sign::Note) {
                sign.note.clef = clefs.at(sign.staff);
                int step = clefStep(sign.note.clef.clefSign, sign.note.step);
                for(int s=2; s<=step; s+=2) { int y=y0+staffY(staff, s); ::fill(target, int2(x-noteSize.x/2,y-1)+Rect(int2(2*noteSize.x,lineWidth))); }
                for(int s=-10; s>=step; s-=2) { int y=y0+staffY(staff, s); ::fill(target, int2(x-noteSize.x/2,y-1)+Rect(int2(2*noteSize.x,lineWidth))); }
                int2 p = int2(x, y0+Y(clefs, staff, sign.note.step));
                Duration duration = sign.note.duration;
                if(duration == Whole) x += 3*glyph(target, p, "noteheads.s0"_, colors.value(noteIndex,black));
                else {
                    x += 3*glyph(target, p, duration==Half?"noteheads.s1"_:"noteheads.s2"_, colors.value(noteIndex,black));
                    if(sign.note.accidental == Flat) glyph(target, p+int2(-noteSize.x,0),"accidentals.flat"_);
                    if(sign.note.accidental == Natural) glyph(target, p+int2(-noteSize.x,0),"accidentals.natural"_);
                    if(sign.note.accidental == Sharp) glyph(target, p+int2(-noteSize.x,0),"accidentals.sharp"_);
                    if(duration<=Eighth) beam << sign;
                    else fill(target, p+Rect(int2(1,tailLength)));
                }
                if(sign.note.dot) glyph(target, p+int2(16,4),"dots.dot"_);
                if(sign.note.staccato) glyph(target, p+int2(noteSize.x/2,-noteSize.y),"scripts.staccato"_);
                array<Sign>& slur = slurs[sign.staff];
                if(slur) slur << sign;
                if(sign.note.slur) {
                    if(!slur) slur << sign; // Starts
                    else { // Stops
                        vec2 p0 = vec2(timeTrack.at(slur.first().time), y0+Y(clefs, slur.first().staff, slur.first().note.step)) + vec2(noteSize.x/2, -2*noteSize.y);
                        vec2 p1 = vec2(p) + vec2(noteSize.x/2, -2*noteSize.y);
                        vec2 k0 = vec2(p0.x, max(p0.y,p1.y)) + vec2(0, -3*noteSize.y);
                        vec2 k0p = k0 + vec2(0, -noteSize.y/2);
                        vec2 k1 = vec2(p1.x, max(p0.y,p1.y)) + vec2(0, -3*noteSize.y);
                        vec2 k1p = k1 + vec2(0, -noteSize.y/2);
                        cubic(target, {p0,k0,k1,p1,k1p,k0p});
                        slur.clear();
                    } // TODO: continue
                }
                noteIndex++;
            }
            else if(sign.type == Sign::Rest) {
                int2 p = int2(x, y0+staffY(staff, -4));
                if(sign.rest.duration == Whole) x+= 3*glyph(target, p, "rests.0"_);
                else if(sign.rest.duration == Half) x+= 3*glyph(target, p, "rests.1"_);
                else if(sign.rest.duration == Quarter) x+= 3*glyph(target, p, "rests.2"_);
                else if(sign.rest.duration == Eighth) x+= 3*glyph(target, p, "rests.3"_);
                else if(sign.rest.duration == Sixteenth) x+= 3*glyph(target, p, "rests.4"_);
                else error(int(sign.rest.duration));
            }
            else if(sign.type == Sign::Measure) {
                if(sign.staff==0) {
                    fill(target, Rect(int2(x-1, y0+staffY(0,0)),int2(x+1, y0+staffY(1,-8))));
                    if(x > int(target.width)) break;
                    x += noteSize.x;
                }
            }
            else if(sign.type == Sign::Dynamic) {
                string word = ref<string>{"ppp"_,"pp"_,"p"_,"mp"_,"mf"_,"f"_,"ff"_,"fff"_}[uint(sign.dynamic.loudness)];
                float w = 0; for(char character: word.slice(0,word.size-1)) w += advance({character}); w += glyphSize({word.last()}).x;
                uint x = timeTrack.at(sign.time).direction;
                x -= w/2; x += glyphSize({word.first()}).x/2;
                for(char character: word) {
                    x += glyph(target, int2(x, y0+staffHeight), {character});
                }
            } else if(sign.type == Sign::Clef) {
                string change = clefs.contains(sign.staff)?"_change"_:""_;
                Clef clef = sign.clef;
                assert_(!clef.octave);
                if(!clefs.contains(sign.staff) || clefs.at(sign.staff).clefSign != sign.clef.clefSign) {
                    clefs[sign.staff] = sign.clef;
                    x += noteSize.x;
                    if(clef.clefSign==Treble) x += glyph(target, int2(x, y0+Y(clefs,staff,4)), "clefs.G"_+change);
                    if(clef.clefSign==Bass) x += glyph(target, int2(x, y0+Y(clefs,staff,-4)),"clefs.F"_+change);
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
                    glyph(target, int2(x, y0+Y(clefs,0,step)), fifths<0?"accidentals.flat"_:"accidentals.sharp"_);
                    x += glyph(target, int2(x, y0+Y(clefs,1,step - (clefs[1u].clefSign==Bass ? 14 : 0))), fifths<0?"accidentals.flat"_:"accidentals.sharp"_);
                }
                x += noteSize.x;
                timeTrack.at(sign.time).direction = x;
            }
            else if(sign.type==Sign::TimeSignature) {
                timeSignature = sign.timeSignature;
                static constexpr string numbers[10] = {"zero"_,"one"_,"two"_,"three"_,"four"_,"five"_,"six"_,"seven"_,"eight"_,"nine"_};
                glyph(target, int2(x, y0+staffY(0, -4)),numbers[timeSignature.beats]);
                glyph(target, int2(x, y0+staffY(1, -4)),numbers[timeSignature.beats]);
                glyph(target, int2(x, y0+staffY(0, -8)),numbers[timeSignature.beatUnit]);
                x += 2*glyph(target, int2(x, y0+staffY(1, -8)),numbers[timeSignature.beatUnit]);
            }
            else if(sign.type == Sign::Metronome) {
                array<uint> word = toUTF32("♩="_+dec(sign.metronome.perMinute));
                uint sx = x;
                for(uint code: word) {
                    uint16 index = textFont.index(code);
                    const Glyph& glyph = textFont.glyph(index);
                    blit(target, int2(sx, y0)+glyph.offset, glyph.image, black);
                    sx += textFont.advance(index);
                }
            }

            if(timeTrack.contains(sign.time+sign.duration)) timeTrack.at(sign.time+sign.duration) = x; // Updates end position for future signs
            else timeTrack.insert(sign.time+sign.duration, x);
        }
    }

    bool mouseEvent(int2, int2, Event, Button button) {
        if(button==WheelUp) { position--; return true; }
        if(button==WheelDown) { position++;return true; }
        return false;
    }
} test;
