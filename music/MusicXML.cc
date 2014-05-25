#include "music.h"
#include "xml.h"

array<Sign> parse(string document, uint& divisions) {
    array<Sign> signs;
    Element root = parseXML(document);
    map<uint, Clef> clefs; map<uint, bool> slurs; KeySignature keySignature={0}; TimeSignature timeSignature={4,4}; uint time = 0, nextTime = 0, maxTime = 0;
    uint measureIndex=1;
    for(const Element& m: root("score-partwise"_)("part"_).children) {
        assert_(m.name=="measure"_, m);
        //log("Measure:", measureIndex);
        for(const Element& e: m.children) {
            if(!(e.name=="note"_ && e("chord"_))) time = nextTime; // Advances time (except chords)
            //log("time", time%(timeSignature.beats*divisions));
            maxTime = max(maxTime, time);

            if(e.name=="note"_) {
                if(e("grace"_)) continue; // FIXME
                uint duration = e("duration"_) ? fromInteger(e("duration"_).text()) : 0;
                if(!e("chord"_)) nextTime = time+duration;
                if(e["print-object"_]=="no"_) continue;
                uint staff = fromInteger(e("staff"_).text())-1;
                Duration type = Duration(ref<string>({"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_}).indexOf(e("type"_).text()));
                assert_(int(type)>=0, e);
                if(e("rest"_)) {
                    {Sign sign{time, duration, staff, Sign::Rest, {}}; sign.rest={type}; signs.insertSorted(sign);}
                } else {
                    assert_(e("pitch"_)("step"_).text(), e);
                    uint octaveStep = "CDEFGAB"_.indexOf(e("pitch"_)("step"_).text()[0]);
                    int octave = fromInteger(e("pitch"_)("octave"_).text());
                    int step = (octave-4) * 7 + octaveStep;
                    Accidental accidental = Accidental(ref<string>({""_,"flat"_,"sharp"_,"natural"_}).indexOf(e("accidental"_).text()));
                    if(e("notations"_)("slur"_)) {
                        if(slurs[staff]) assert_(e("notations"_)("slur"_).attribute("type"_)=="stop"_);
                        else assert_(e("notations"_)("slur"_).attribute("type"_)=="start"_, e("notations"_)("slur"_).attribute("type"_), e);
                        slurs[staff] = !slurs[staff];
                    }
                    {Sign sign{time, duration, staff, Sign::Note, {}};
                        sign.note={clefs.at(staff), step, accidental, type,
                                   e("dot"_) ? true : false,
                                   e("notations"_)("slur"_)?true:false,
                                   e("grace"_)?true:false,
                                   e("notations"_)("articulations"_)("staccato"_)?true:false,
                                   e("notations"_)("articulations"_)("tenuto"_)?true:false,
                                   e("notations"_)("articulations"_)("accent"_)?true:false,
                                   e("stem").text() == "up"_
                                  };
                        signs.insertSorted(sign);};
                }
            }
            else if(e.name=="backup"_) {
                int dt = fromInteger(e("duration"_).text());
                time -= dt;
                //log("<<", dt);
                nextTime = time;
            }
            else if(e.name=="forward"_) {
                int dt =  fromInteger(e("duration"_).text());
                time += dt;
                //log(">>", dt);
                maxTime = max(maxTime, time);
                nextTime = time;
            }
            else if(e.name=="direction"_) {
                const Element& d = e("direction-type"_);
                if(d("dynamics"_)) {
                    Loudness loudness = Loudness(ref<string>({"ppp"_,"pp"_,"p"_,"mp"_,"mf"_,"f"_,"ff"_,"fff"_}).indexOf(d("dynamics"_).children.first()->name));
                    {Sign sign{time, 0, 0, Sign::Dynamic, {}}; sign.dynamic={loudness}; signs << sign;}
                }
                else if(d("metronome"_)) {
                    Duration beatUnit = Duration(ref<string>({"whole"_,"half"_,"quarter"_,"eighth"_,"16th"_}).indexOf(d("metronome"_)("beat-unit"_).text()));
                    uint perMinute = fromInteger(d("metronome"_)("per-minute"_).text());
                    {Sign sign{time, 0, 0, Sign::Metronome, {}}; sign.metronome={beatUnit, perMinute}; signs << sign;}
                }
                else if(d("pedal"_)) {
                    PedalAction action = PedalAction(ref<string>({"start"_,"change"_,"stop"_}).indexOf(d("pedal"_)["type"_]));
                    if(action==Start && d("pedal"_)["line"_]!="yes"_) action=Ped;
                    int offset = e("offset"_) ? fromInteger(e("offset"_).text()) : 0;
                    {Sign sign{time + offset, 0, 0, Sign::Pedal, {}}; sign.pedal={action}; signs.insertSorted(sign);}
                }
                else if(d("wedge"_)) {
                    WedgeAction action = WedgeAction(ref<string>({"crescendo"_,"diminuendo"_,"stop"_}).indexOf(d("wedge"_)["type"_]));
                    {Sign sign{time, 0, 0, Sign::Wedge, {}}; sign.wedge={action}; signs << sign;}
                }
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
        maxTime = max(maxTime, time);
        time=maxTime;
        measureIndex++;
        {Sign sign{time, 0, 0, Sign::Measure, {}}; sign.measure.index=measureIndex; signs.insertSorted(sign);}
        {Sign sign{time, 0, 1, Sign::Measure, {}}; sign.measure.index=measureIndex; signs.insertSorted(sign);}
        //if(time%(timeSignature.beats*divisions)!=0) break;
        //assert_(time%(timeSignature.beats*divisions)==0, measureIndex, time, timeSignature.beats, divisions);
    }
    return signs;
}
