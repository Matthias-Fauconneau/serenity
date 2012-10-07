#include "score.h"

void Score::onPath(const ref<vec2>& p) {
    vec2 min=p[0], max=p[0]; for(vec2 a: p) min=::min(min,a), max=::max(max,a);
    vec2 center = (min+max)/2.f; vec2 span=max-min;
    if(p.size==2) {
        if(p[0].x==p[1].x && abs(p[0].y-p[1].y)>20 && abs(p[0].y-p[1].y)<70) {
            tails << Line(p[0], p[1]);
        }
    } else if(p.size==5) {
        if( (abs(p[0].y-p[2].y)<20 || abs(p[1].y-p[3].y)<20) &&
                abs(p[0].x-p[2].x)>10 && abs(p[0].x-p[2].x)<500) {
            tremolos << Line(p[0], p[2]);
        }
    } else if((p.size==4&&p[1]!=p[2]&&p[2]!=p[3])||p.size==7) {
        if(span.y>10 && abs(p[0].y-p[3].y)<1) {
            ties+= Line(p[0],p[3]);
        }
    } else if(p.size==10) {
        if(span.x>36 && span.x<1000 && span.y>10 && span.y<40) {
            debug[center]=str(span);
            ties+= Line(vec2(min.x,center.y),vec2(max.x,center.y));
        }
    }
}

void Score::onGlyph(int index, vec2 pos, float size,const ref<byte>& font, int code) {
    uint i=0; for(;i<staffs.size() && pos.y>staffs[i];i++) {}
    if(i>=notes.size()) notes.grow(i+1);
    int duration=-1;
    if(find(font,"LilyPond"_)) {
        if(code==62) {
            if(size<30) duration= 0; //grace
            else duration = 4; //quarter
        }
        else if(code==61) duration = 8; //half
        else if(code==60) duration = 16; //whole
        else if((code==147/*treble*/||code==145/*bass*/) && pos.x<200) {
            if(pos.y-lastClef.y>202) staffs << (lastClef.y+100);
            if(pos.y>lastClef.y) lastClef=pos;
        }
    } else if(find(font,"Opus"_)||find(font,"Emmentaler"_)) {
        if(font=="OpusStd"_) {
            if(code==8) {
                if(size<30) duration= 0; //grace
                else duration = 4; //quarter
            }
            else if(code==9) duration = 8; //half
            else if(code==16) duration = 16; //whole
            else if(code==3/*treble*/||code==5/*bass*/) {
                if(pos.y-lastClef.y>202) {
                    staffs << (lastClef.y+90);
                    //{ static uint min=-1; int y=pos.y-lastClef.y; if(uint(y)<min) min=y, log(pos.y-lastClef.y); }
                }
                lastClef=pos;
            }
        } else if(endsWith(font,"Opus"_)||find(font,"Emmentaler"_)) {
            if(code==53/*Opus*/ || code==1/*Emmentaler*/) {
                if(size<30) duration= 0; //grace
                else duration = 4; //quarter
            }
            else if(code==66) duration = 8; //half
            else if(code==39) duration = 16; //whole
            else if(code==71/*Opus treble*/||code==11/*Opus bass*/||code==214/*Emmentaler C key*/) {
                if(pos.y-lastClef.y>154) staffs << (lastClef.y+90);
                lastClef=pos;
            }
        }
        if(code==56/*trill head*/) trills << Line(pos,pos);
        else if(code==41/*trill tail*/ && trills && abs(trills.last().b.y-pos.y)<16) {
            trills.last().b=pos;
        }
        else if(code==58/*dot*/) {
            for(int x : notes[i].keys) {
                if(x>pos.x-16) break;
                if(x>pos.x-48) for(int y : notes[i][x].keys) if(-y>pos.y-16&&-y<pos.y+32) notes[i][x].at(y).duration *= 3/2;
            }
            dots[i] << pos;
            map<float, vec2> matches;
            for(vec2 dot : dots[i]) if(abs(dot.x-pos.x)<1) matches[dot.y]=dot;
            const array<float>& y = matches.keys; const array<vec2>& m = matches.values;
            /*log("dot",pos,"on vertical line",m);
            if(m.size()>=2) log("01", abs(y[0]-y[1]));
            if(m.size()>=3) log("12",abs(y[1]-y[2]));
            if(m.size()>=4) log("23",abs(y[2]-y[3]));*/
            if(m.size()==4) {
                if(abs(y[0]-y[1])<13 && abs(y[1]-y[2])>=122 && abs(y[2]-y[3])<13 ) repeats<<(m[0]+m[1]+m[2]+m[3])/4.f;
                //else log("!REPEAT?");
            }
        }
        /*static QPoint lastMeter; static int beatsPerMeasure,beatUnit;
        if(code==1||code==2||code==3||code==4||code==8||code==9) {//time signature
            if(lastMeter.isNull()) { beatsPerMeasure=code; lastMeter=pos; }
            else if(pos.y==lastMeter.y) { beatsPerMeasure*=10; beatsPerMeasure+=code; lastMeter=pos; }
            else { beatUnit=code; lastMeter=QPoint(); }
        }*/
    } else if(find(font,"DUCRGK"_)) { //TODO: glyph OCR
        //TODO: quaver, dotted notes
        if(code==7) {
            if(size<2) duration = 0; //grace
            else duration = 4; //quarter
        }
        else if(code==8) duration = 8; //half
        //else if(code==9) duration = 16; //whole
        else if(code==1/*treble*/||code==5/*bass*/) {
            if(lastClef.y != 0 && pos.y-lastClef.y>128) staffs << (lastClef.y+96);
            lastClef=pos;
        }
    }
    if(duration>=0 && !notes[i].sorted(pos.x).contains(-pos.y)) {
        /*for(int x: range(1,22)) if(notes[i].contains(pos.x-x)) {
            for(pair<int,Note> note: notes[i][pos.x-x]) { //move previous notes to this one
                if(!notes[i].sorted(pos.x).contains(note.key)) { //double note error
                    notes[i].sorted(pos.x).insertSorted(note.key, note.value);
                    debug[vec2(pos.x-x,-note.key)]=str("M"_,x);
                }
            }
            notes[i].remove(pos.x-x);
            break;
        }
        for(int x: range(1,32)) if(notes[i].contains(pos.x-x) && notes[i].sorted(pos.x-x).contains(-pos.y)) {
            notes[i].sorted(pos.x-x).remove(-pos.y); //double note
            debug[vec2(pos.x-x,-pos.y)]=string("**"_);
        }*/
        notes[i].sorted(pos.x).insertSorted(-pos.y, Note(index,duration));
    }
}

struct Tie { uint li; int lx,ly; uint ri; int rx,ry; int dy; Tie():li(0),lx(0),ly(0),ri(0),rx(0),ry(0){}};
string str(const Tie& t) { return "Tie("_+str(t.li,t.lx,t.ly,"-",t.ri,t.rx,t.ry)+")"_; }
void Score::synchronize(map<int,Chord>&& chords) {
    if(!staffs) return; assert(staffs);
    staffs << (lastClef.y+96); //add a last split at the bottom of the last page
    //uint i=0; for(Staff& staff: notes) { for(int x : staff.keys) for(int y : staff.at(x).keys) debug[vec2(x+16,-y)]=str(i); i++; } //assert proper staff sorting

    /// Detect and remove tied notes
    array<Tie> tied;
    for(Line tie : ties) {
        int l = abs(tie.b.x-tie.a.x);
        uint i=0; for(;i<staffs.size()-1 && tie.a.y>staffs[i];i++) {}
        int noteBetween=0; Tie t;
        for(int x : notes[i].keys) {
            int lx = x-tie.a.x;
            int rx = x-tie.b.x;
            for(int y : notes[i][x].keys) {
                int ly = -y-tie.a.y;

                /// Detect first note of a tie
                if(!t.ly || abs(ly)<abs(t.dy)) {
                    if(notes[i][x].at(y).duration>0/*not grace*/ && lx < 4 && lx>-34 && ly<15 && rx<-34) {
                        debug[vec2(x,-y)]=string("L"_);
                        for(Tie t2 : tied) if(t2.li==i && t2.lx==x && t2.ly==y) goto alreadyTied; //debug[vec2(x,-y)]=string("&&"_+str(lx,ly,rx,ry));
                        t.li=i; t.lx=x; t.ly=y; t.dy=ly;
                    } else if(lx>-50 && lx<10 && ly<20 && rx<-30) debug.insertMulti(vec2(x+16,-y-16),string("!L"_+str(lx,ly)));
                }
            }
            for(int y : notes[i][x].keys) {
                int ry = y-t.ly;
#if 0
                /// Detect if there is a note between the tied notes (necessary to sync with HTTYD sheets)
                if(!noteBetween && lx > 0 && rx < -16 && abs(ry) < 7) {
                    debug[vec2(x,-y)]=string("B"_);
                    noteBetween++;
                    if(abs(x-t.lx)<32) noteBetween++;
                    break;
                }
                /// Remove every other note between tied notes (necessary to sync with HTTYD sheets)
                if(noteBetween%2 && lx > 0 && rx < -16 && abs(ry) < 2 && l < 200) {
                    debug[vec2(x,-y)]=string("O"_);
                    t.ri=i;t.rx=x; t.ry=y; tied<<t; //notes[i][x].remove(y);
                    noteBetween++;
                }
#endif
                /// Detect right note of a tie
                if( (!noteBetween || (noteBetween<2 && l<210)) && ry>-7 && ry < 7 && rx < 21 && rx > -10/*-9*//*-12*/) {
                    t.ri=i;t.rx=x; t.ry=y;
                    tied << t; //defer remove for double ties
                    debug[vec2(x,-y)]=string("R"_+str(rx,ry));
                    goto staffDone;
                } else if(rx>-10 && rx<30 && ry>-10 && ry<10) debug[vec2(x,-y-16)]="!R"_+str(rx,ry);
            }
alreadyTied: ;
        }
staffDone: ;
        /// Detect notes tied over a line wrap
        if(t.ly && (!noteBetween || (noteBetween<2 && l>150)) && i+1<staffs.size() && tie.b.x > notes[i].keys.last()+10 ) {
            for(int x=0;x<2;x++) {
                int rx = notes[i+1].keys[x];
                int ry = notes[i+1].values[x].keys[0];
                for(Line trill : trills) if(abs(rx-trill.a.x)<8 && -ry-trill.a.y>0 && -ry-trill.a.y<200) goto trillCancelTie;
                float min=12;
                for(float y2 : notes[i+1].values[x].keys) {float dy = (-y2-staffs[i+1])-(-t.ly-staffs[i]); if(dy>=0) min=::min(min, abs(dy));}
                for(float y2 : notes[i+1].values[x].keys) {
                    int dy = (-y2-staffs[i+1])-(-t.ly-staffs[i]);
                    if(dy>=-7 && abs(dy)<=min) {
                        t.ri=i+1;t.rx=rx; t.ry=y2;
                        debug[tie.a]=string("W"_);
                        debug[vec2(x,-y2)]=string("W"_);
                        for(Tie o: tied) if(t.ri == o.ri && t.rx == o.rx && t.ry==o.ry)
                            //error(-t.ly-staffs[i]-(-y2-staffs[i+1]), -o.ly-staffs[i]-(-y2-staffs[i+1]));
                            goto alreadyTied2;
                        tied << t;
                        goto tieFound;
                    } //else if(abs((-t.ly-staffs[i])-(-y2-staffs[i+1]))<100) debug[vec2(rx,-y2)]="Y"_+str(dy,min);
alreadyTied2: ;
                }
            }
tieFound: ;
trillCancelTie: ;
        } //else debug[tie.a]=str("!W"_,noteBetween,tie.b.x,notes[i].keys.last()+10);
    }
    for(Tie t : tied) if(notes[t.ri][t.rx].contains(t.ry)) notes[t.ri][t.rx].remove(t.ry);

    /// Fix chords with diadics (shifted x positions) or double notes
    for(map<int, map< int, Note> >& staff : notes) {
        int lastX=0;
        for(int x : staff.keys) {
            if(lastX>0) {
                again: ;
                for(int y: staff[x].keys) {
                    for(int y2 : staff[lastX].keys) {
                        if(staff[lastX].at(y2).duration && (
                                    abs(x-lastX)<2 ||
                                    (abs(x-lastX)<=9 && (staff[lastX].size()>1 || staff[x].size()>1)) ||
                                    ((abs(x-lastX)<18 && abs(x-lastX)+abs(y-y2)<38) && (y!=y2 || staff[lastX].size()>1 || staff[x].size()>1)))) {
                            if(staff[lastX].size()>=staff[x].size()) {
                                if(!staff[lastX].contains(y)) staff[lastX].insertSorted(y,staff[x].at(y));
                                staff[x].remove(y); debug[vec2(x,-y)]=str("<-"_,x-lastX,y-y2); goto again;
                            } else if(staff[lastX].size()<staff[x].size()) {
                                if(!staff[x].contains(y2)) staff[x].insertSorted(y2,staff[lastX].at(y2));
                                staff[lastX].remove(y2); debug[vec2(lastX,-y2)]=str("->"_,x-lastX,y-y2); goto again;
                            }
                        } //else if(abs(x-lastX)<10 || (abs(x-lastX)<40 && abs(x-lastX)+abs(y-y2)<40)) debug[vec2(x,-y)]="?"_+str(x-lastX,y-y2);
                    }
                }
            }
            lastX=x;
        }
    }

    /// Detect and explicit tremolos
    /*for(int i=0;i<tremolos.size-2;i++) {
        if(tremolos[i].x1()==tremolos[i+1].x1()) {
            foreach(QLineF tail,tails) if(abs(tail.x1()-tremolos[i].x1())<6 && abs(tail.y2()-tremolos[i].y1())<60) goto spurious;
            {
                //debug << Debug(QPoint(tremolos[i].x1(),tremolos[i].y1()), "tremolos");
                int staff=0; for(;staff<staffs.size-1 && tremolos[i].y1()>staffs[staff];staff++) {}
                int prevX=0,X1=0,X2=0,Y1=0,Y2=0;
                foreach(int x, notes[staff].keys()) {
                    foreach(int y, notes[staff][x].keys()) if(abs(-y-tremolos[i].y1())<60) goto inRange;
                    continue;
inRange: ;
                    if(!X1 && x>tremolos[i].x1()) { X1=prevX; Y1=notes[staff][X1].keys()[0]; }
                    if( x+10>tremolos[i].x2() ) {X2=x;Y2=notes[staff][x].keys()[0];break;}
                    prevX=x;
                }
                if(X1&&X2) {
                    int duration = notes[staff][X1][Y1]+notes[staff][X2][Y2];
                    notes[staff][X2].remove(Y2);
                    int times = duration*((tremolos[i].x1()==tremolos[i+2].x1())?4:2)/8;
                    for(int t=0;t<times;t++) {
                        notes[staff][X1+(X2-X1)*(2*t+0)/times][Y1]=duration/times;
                        notes[staff][X1+(X2-X1)*(2*t+1)/times][Y2]=duration/times;
                    }
                }
                if(tremolos[i].x1()==tremolos[i+2].x1()) i+=2; else i++;
            }
spurious: ;
        }
    }*/

    /// Detect and explicit trills
    /*foreach(QLineF trill, trills) {
        int i=0; for(;i<staffs.size && trill.y1()>staffs[i];i++) {}
        foreach(int x, notes[i].keys()) {
            if(x>trill.x1()-8) {
                foreach(int y, notes[i][x].keys())
                    if(abs(-y-trill.y1())<16) {
                        int duration = notes[i][x][y];
                        int times = duration*3/4;
                        for(int t=0;t<times;t++) {
                            if(t>0) notes[i][trill.x1()+(trill.x2()-trill.x1())*(2*t+0)/(2*times)][y]=duration/times;
                            if(t<times-1) notes[i][trill.x1()+(trill.x2()-trill.x1())*(2*t+1)/(2*times)][y+8]=duration/times;
                        }
                        break;
                    }
                break;
            }
        }
    }*/

    /// Flatten sorted notes
    uint n=0; for(Staff& staff: notes) for(int x : staff.keys) for(int y : staff.at(x).keys) { positions<<vec2(x,-y); indices<<staff[x].at(y).index; staff[x].at(y).scoreIndex=n; n++; }

    /// Detect and explicit repeats
    /*int startIndex=-1;
    for(vec2 pos : repeats) {
        uint i=0; for(;i<staffs.size()-1 && pos.y>staffs[i];i++) {}
        int index=notes[i].values[0].values[0].scoreIndex-1;
        for(int x : notes[i].keys) { if(x>pos.x) break; index=notes[i][x].values[0].scoreIndex; }
        if(startIndex < 0) {
            startIndex=index; debug[pos]=dec(startIndex)+"{"_;
        } else {
            assert(index>startIndex);
            { array<vec2> cat; cat<<positions.slice(0,index+1)<<positions.slice(startIndex+1); positions = move(cat); }
            { array<int> cat; cat<<indices.slice(0,index+1)<<indices.slice(startIndex+1); indices = move(cat); }
            startIndex=-1;
            debug[pos]="}"_+dec(index);
        }
    }*/

    /// Synchronize notes to MIDI track
    array<int> MIDI; //flatten chords for robust MIDI synchronization
    for(const Chord& chord: chords.values) MIDI<<chord;
    vec2 lastPos=vec2(0,0); int lastNote=0;
    for(uint i=0; i<MIDI.size() && i<positions.size();) {
        vec2 pos=positions[i]; int note = MIDI[i];
        /*if(n<MIDI.size() && MIDI[n]==note && lastY && y!=lastY && abs(y-lastY)<16) { //double notes in MIDI
                    debug[vec2(x,-y)]=string("+++"_);
                    chord<<vec2(x,-y); positions<<vec2(x,-y); indices<<staff[x].at(y).index; staff[x].at(y).scoreIndex=n; n++;
                }*/
        /*if(pos.y==lastPos.y && note!=lastNote) { // double notes in score
            debug[pos]=string("////"_);
            positions.removeAt(i); indices.removeAt(i);
        } else*/ if(lastPos && lastNote && pos.x==lastPos.x && pos.y<lastPos.y && note<lastNote) { // missing note in MIDI
            debug[pos]=string("|||||"_);
            positions.removeAt(i); indices.removeAt(i);
        } else {
            debug.insertMulti(positions[i]+vec2(12,0),dec(MIDI[i]));
            i++;
        }
        lastPos=pos; lastNote=note;
    }

    for(Line l: ties) debug[(l.a+l.b)/2.f]=string("^"_);
    for(float y: staffs) debug[vec2(0,y-16)]=string("___________"_);

    //log(chords);
    this->chords=move(chords);
    //dots.clear(); notes.clear(); repeats.clear(); ties.clear(); tails.clear(); tremolos.clear(); trills.clear();
}

void Score::seek(uint unused time) {
    if(!staffs) return;
    assert(time==0,"TODO");
    chordIndex=0, noteIndex=0; currentStaff=0; expected.clear(); active.clear();
    int i=noteIndex; for(int key: chords.values[chordIndex]) {
        expected.insertMulti(key, i);
        while(positions[i].y>staffs[currentStaff] && currentStaff<staffs.size()-1) {
            assert(currentStaff<staffs.size());
            if(currentStaff>0) nextStaff(staffs[currentStaff-1],staffs[currentStaff],staffs[min(staffs.size()-1,currentStaff+1)]);
            currentStaff++;
        }
        i++;
    }
    map<int,byte4> activeNotes;
    for(int i: expected.values) activeNotes.insert(indices[i],blue);
    activeNotesChanged(activeNotes);
}

void Score::noteEvent(int key, int vel) {
    if(vel) {
        if(!expected.contains(key)){/*log(key,"expected",expected);*/ return;}
        active.insertMulti(key,expected.at(key));
        expected.remove(key);
    } else if(key) {
        if(active.contains(key)) active.remove(key);
        return;
    }
    if(!expected && chordIndex<chords.size()-1) {
        noteIndex+=chords.values[chordIndex].size();
        chordIndex++;
        int i=noteIndex; for(int key: chords.values[chordIndex]) {
            expected.insertMulti(key, i);
            while(positions[i].y>staffs[currentStaff] && currentStaff<staffs.size()-1) {
                assert(currentStaff<staffs.size());
                if(currentStaff>0) nextStaff(staffs[currentStaff-1],staffs[currentStaff],staffs[min(staffs.size()-1,currentStaff+1)]);
                currentStaff++;
            }
            i++;
        }
    }
    map<int,byte4> activeNotes;
    for(int i: expected.values) activeNotes.insertMulti(indices[i],blue);
    //for(int i: active.values) if(!activeNotes.contains(indices[i])) activeNotes.insert(indices[i],red);
    activeNotesChanged(activeNotes);
}
