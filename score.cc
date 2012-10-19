#include "score.h"

void Score::onPath(const ref<vec2>& p) {
    vec2 min=p[0], max=p[0]; for(vec2 a: p) min=::min(min,a), max=::max(max,a);
    vec2 center = (min+max)/2.f; vec2 span=max-min;
    if(p.size==2) {
        if(p[0].x==p[1].x && abs(p[0].y-p[1].y)>20 && abs(p[0].y-p[1].y)<70) {
            tails << Line(p[0], p[1]);
        }
    } else if(p.size==5||p.size==13) {
        if(span.x > 75 && span.x < 76 && span.y > 8 && span.y < 18) {
            tremolos << Line(p[0], p[3]);
        }
    } else if((p.size==4&&p[1]!=p[2]&&p[2]!=p[3])||p.size==7) {
        if(span.y>2 && span.x<355 && (span.y<14 || (span.x>90 && span.y<17) || (span.x>100 && span.y<20) || (span.x>200 && span.y<27) || (span.x>300 && span.y<30))) {
            //debug[center]="V"_+str(span);
            ties+= Line(vec2(min.x,p[0].y),vec2(max.x,p[3].y));
        } //else debug[center]="!"_+str(span);
    } else if(p.size==10) {
        if(span.x>36 && span.x<1000 && span.y>10 && (span.y<14 || (span.x>100 && span.y<29))) {
            //debug[center]="X"_+str(span);
            ties+= Line(vec2(min.x,center.y),vec2(max.x,center.y));
        }
    }
}

void Score::onGlyph(int index, vec2 pos, float size,const ref<byte>& font, int code) {
    if(index == 0) pass++;
    //TODO: map font dependent codes to unique enum, and factorize logic
    //log(font); debug[pos]=dec(code);
    if(pass==0) { // first pass: split in staffs
        if(font=="MScore-20"_) { //TODO: glyph OCR
            if((code==1||code==12/*treble*/||code==2||code==13/*bass*/) && pos.x<200) {
                if(pos.y-lastClef.y>170) staffs << (lastClef.y+pos.y)/2;//(lastClef.y+100);
                if(pos.y>lastClef.y) lastClef=pos;
                if(code==12 || code==13) msScore=1;
            }
        } else if(find(font,"LilyPond"_)) {
          if((code==147/*treble*/||code==145/*bass*/) && pos.x<200) {
                if(pos.y-lastClef.y>201) staffs << (lastClef.y+pos.y)/2;//(lastClef.y+100);
                //else { static uint max=0; int y=pos.y-lastClef.y; if(uint(y)>max) max=y, log(pos.y-lastClef.y); }
                if(pos.y>lastClef.y) lastClef=pos;
            }
        } else if(font=="OpusStd"_) {
            if(code==3/*treble*/||code==5/*bass*/) {
                if(pos.y-lastClef.y>202) {
                    staffs << (lastClef.y+pos.y)/2;//(lastClef.y+100);
                    //{ static uint min=-1; int y=pos.y-lastClef.y; if(uint(y)<min) min=y, log(pos.y-lastClef.y); }
                }
                lastClef=pos;
            }
        } else if(endsWith(font,"Opus"_)) {
            if(code==71/*treble*/||code==11/*bass*/) {
                if(pos.y-lastClef.y>159) {
                    staffs << (lastClef.y+pos.y)/2; //(lastClef.y+90/*100*/); //90-100-120
                    //{ static uint min=-1; int y=pos.y-lastClef.y; if(uint(y)<min) min=y, log(pos.y-lastClef.y); }
                }
                lastClef=pos;
            }
         } else if(find(font,"DUCRGK"_)) { //TODO: glyph OCR
            if(code==1/*treble*/||code==5/*bass*/) {
                if(lastClef.y != 0 && pos.y-lastClef.y>128) staffs << (lastClef.y+110);
                lastClef=pos;
            }
        } else if(find(font,"ZVBUUH"_)) { //TODO: glyph OCR
            if(code==1/*treble*/||code==2/*bass*/) {
                if(lastClef.y != 0 && pos.y-lastClef.y>159) staffs << (lastClef.y+pos.y)/2;//(lastClef.y+110);
                lastClef=pos;
            }
        } else if(find(font,"Inkpen2"_)) { //TODO: glyph OCR
            if(code==3/*treble*/||code==12/*bass*/) {
                if(lastClef.y != 0 && pos.y-lastClef.y>128) staffs << (lastClef.y+pos.y)/2;//(lastClef.y+110);
                lastClef=pos;
            }
        } else if(find(font,"NWCV15"_)) { //TODO: glyph OCR
            if(code==97/*treble*/||code==98/*bass*/) {
                if(lastClef.y != 0 && pos.y-lastClef.y>128) staffs << (lastClef.y+pos.y)/2;
                lastClef=pos;
            }
        }
    } else if(pass==1) {
        uint i=0; for(;i<staffs.size() && pos.y>staffs[i];i++) {}
        if(i>=notes.size()) notes.grow(i+1);
        int duration=-1;
        if(font=="MScore-20"_) { //TODO: glyph OCR
            if(msScore) {
                if(code==14) {
                    if(size<30) duration= 0; //grace
                    else duration = 4; //quarter
                }
                else if(code==15) duration = 8; //half
                else if(code==16) duration = 16; //whole
            } else {
                if(code==5) {
                    if(size<30) duration= 0; //grace
                    else duration = 4; //quarter
                }
                else if(code == 9) duration = 8; //half
            }
        } else if(find(font,"LilyPond"_)) {
            if(code==62) {
                if(size<30) duration= 0; //grace
                else duration = 4; //quarter
            }
            else if(code==61) duration = 8; //half
            else if(code==60) duration = 16; //whole
        } else if(find(font,"Opus"_)) {
            if(font=="OpusStd"_) {
                if(code==8) {
                    if(size<30) duration= 0; //grace
                    else duration = 4; //quarter
                }
                else if(code==9) duration = 8; //half
                else if(code==16) duration = 16; //whole
            } else if(endsWith(font,"Opus"_)) {
                if(code==53) {
                    if(size<30) duration= 0; //grace
                    else duration = 4; //quarter
                }
                else if(code==66) duration = 8; //half
                else if(code==39) duration = 16; //whole
            }
            if(code==41 && trills && abs(trills.last().b.y-pos.y)<16) { trills.last().b=pos; debug[pos]=string("Trill"_); } //trill tail
            else if(code==56) { trills << Line(pos,pos); debug[pos]=string("Trill"_); } //trill head
            else if(code==58) { //dot
                dots[i] << pos;
                map<float, vec2> matches;
                for(vec2 dot : dots[i]) if(abs(dot.x-pos.x)<1) matches[dot.y]=dot;
                const array<float>& y = matches.keys; const array<vec2>& m = matches.values;
                if(m.size()==4 && abs(y[0]-y[1])<13 && abs(y[1]-y[2])>=122 && abs(y[2]-y[3])<13 ) {
                    vec2 pos = (m[0]+m[1]+m[2]+m[3])/4.f;
                    uint i=0; for(;i<repeats.size() && repeats[i].y*1000+repeats[i].x < pos.y*1000+pos.x;i++) {} repeats.insertAt(i,pos);
                }
            } else if(code==77) { //tremolo
                tremolos << Line(pos,pos);
            }
        } else if(find(font,"DUCRGK"_)) { //TODO: glyph OCR
            if(code==7) {
                if(size<2) duration = 0; //grace
                else duration = 4; //quarter
            }
            else if(code==8) duration = 8; //half
        } else if(find(font,"ZVBUUH"_)) { //TODO: glyph OCR
            if(code==4) {
                if(size<2) duration = 0; //grace
                else duration = 4; //quarter
            }
            else if(code==8) duration = 8; //half
            else if(code==9) duration = 16; //whole
        } else if(find(font,"Inkpen2"_)) { //TODO: glyph OCR
            if(code==5) {
                if(size<2) duration = 0; //grace
                else duration = 4; //quarter
            }
            else if(code==15) duration = 8; //half
            else if(code==22) duration = 16; //whole
        } else if(find(font,"NWCV15"_)) { //TODO: glyph OCR
            if(code==107) {
                if(size<2) duration = 0; //grace
                else duration = 4; //quarter
            }
            else if(code==106) duration = 8; //half
            else if(code==105) duration = 16; //whole
        }
        if(duration>=0 && !notes[i].sorted(pos.x).contains(-pos.y)) notes[i].sorted(pos.x).insertSorted(-pos.y, Note(index,duration));
    }
}

struct Tie { uint li; int lx,ly; uint ri; int rx,ry; int dy; Tie():li(0),lx(0),ly(0),ri(0),rx(0),ry(0){}};
string str(const Tie& t) { return "Tie("_+str(t.li,t.lx,t.ly,"-",t.ri,t.rx,t.ry)+")"_; }
void Score::parse() {
    if(!staffs) return; assert(staffs);
    staffs << (lastClef.y+96); //add a last split at the bottom of the last page
    //uint i=0; for(Staff& staff: notes) { for(int x : staff.keys) for(int y : staff.at(x).keys) debug[vec2(x+16,-y)]=str(i); i++; } //assert proper staff sorting

    /// Lengthens dotted notes
    for(pair< int,array<vec2> > dots: this->dots) {
        for(vec2 pos: dots.value) for(int x : notes[dots.key].keys) {
            if(x>pos.x-16) break;
            if(x>pos.x-48) for(int y : notes[dots.key][x].keys) if(-y>pos.y-16&&-y<pos.y+32) notes[dots.key][x].at(y).duration = notes[dots.key][x].at(y).duration*3/2;
        }
    }

    /// Detect and remove tied notes
    array<Tie> tied;
    for(Line tie : ties) {
        int l = abs(tie.b.x-tie.a.x);
        uint staff=0; for(;staff<staffs.size()-1 && tie.a.y>staffs[staff];staff++) {}
        int noteBetween=0; Tie t;
        for(uint i=staff-1;i<staff+1;i++) {
            for(int x : notes[i].keys) {
                int lx = x-tie.a.x;
                int rx = x-tie.b.x;
                for(int y : notes[i][x].keys) {
                    int ly = -y-tie.a.y;

                    /// Detect first note of a tie
                    if(!t.ly || abs(ly)<abs(t.dy)) {
                        if(notes[i][x].at(y).duration>0/*not grace*/ && lx < 4 && lx>-49 && ly>-34 && ly<15 && rx<1) {
                            //debug[vec2(x,-y)]=string("L"_);
                            for(Tie t2 : tied) if(t2.li==i && t2.lx==x && t2.ly==y) goto alreadyTied; //debug[vec2(x,-y)]=string("&&"_+str(lx,ly,rx,ry));
                            t.li=i; t.lx=x; t.ly=y; t.dy=ly;
                        } else if(lx>-49 && lx<4 && ly>-40 && ly<20 && rx<1) debug.insertMulti(vec2(x,-y),string("!L"_+str(lx,ly,rx)));
                    }
alreadyTied: ;
                }
                for(int y : notes[i][x].keys) {
                    int ry = y-t.ly;
#if 1
                    /// Detect if there is a note between the tied notes (necessary to sync with HTTYD sheets)
                    if(!noteBetween && lx > 0 && rx < -16 && abs(ry) < 7) {
                        debug[vec2(x,-y)]=string("B"_);
                        noteBetween++;
                        if(abs(x-t.lx)<32) noteBetween++;
                        break;
                    }
#endif
#if 0
                    /// Remove every other note between tied notes (necessary to sync with HTTYD sheets)
                    if(noteBetween%2 && lx > 0 && rx < -16 && abs(ry) < 2 && l < 200) {
                        debug[vec2(x,-y)]=string("O"_);
                        t.ri=i;t.rx=x; t.ry=y; tied<<t; //notes[i][x].remove(y);
                        noteBetween++;
                    }
#endif
                    /// Detect right note of a tie
                    if( /*(!noteBetween || (noteBetween<2 && l<210)) &&*/ ry>-6 && ry < 7 && rx < 21 && rx > -10/*-9*//*-12*/) {
                        t.ri=i;t.rx=x; t.ry=y;
                        tied << t; //defer remove for double ties
                        //debug[vec2(x,-y-24)]=string("R"_+str(rx,ry));
                        goto staffDone;
                    } //else if(rx>-40 && rx<40 && ry>-40 && ry<40) debug.insertMulti(vec2(x,-y+16),str("!R"_,rx,ry));
                }
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
                            debug[vec2(rx,-y2)]=string("W"_);
                            for(Tie o: tied) if(t.ri == o.ri && t.rx == o.rx && t.ry==o.ry)
                                //error(-t.ly-staffs[i]-(-y2-staffs[i+1]), -o.ly-staffs[i]-(-y2-staffs[i+1]));
                                goto alreadyTied2;
                            tied << t;
                            goto tieFound;
                        } else if(abs((-t.ly-staffs[i])-(-y2-staffs[i+1]))<100) debug[vec2(rx,-y2)]="Y"_+str(dy,min);
alreadyTied2: ;
                    }
                }
tieFound: ;
trillCancelTie: ;
            } //else debug[tie.a]=str("!W"_,noteBetween,tie.b.x,notes[i].keys.last()+10);
        }
    }
    for(Tie t : tied) if(notes[t.ri][t.rx].contains(t.ry)) notes[t.ri][t.rx].remove(t.ry);

    /// Fix chords with diadics (shifted x positions) or double notes (TODO: use MIDI assistance)
    for(map<int, map< int, Note> >& staff : notes) {
        int lastX=0;
        for(int x : staff.keys) {
            if(lastX>0) {
                again: ;
                for(int y: staff[x].keys) {
                    for(int y2 : staff[lastX].keys) {
                        if(staff[lastX].at(y2).duration && (
                                    abs(x-lastX)<2 ||
                                    (abs(x-lastX)<10 && abs(y-y2)<180 && (staff[lastX].size()>1 || staff[x].size()>1)) ||
                                    ((abs(x-lastX)<18 && abs(y-y2)<20) && (y!=y2 || staff[lastX].size()>1 || staff[x].size()>1))
                                    //|| (abs(x-lastX)<19/*25*/ && abs(y-y2)<6) //TODO: relative to average note distance
                                    )) {
                            if(staff[lastX].size()>=staff[x].size()) {
                                if(!staff[lastX].contains(y)) staff[lastX].insertSorted(y,staff[x].at(y));
                                staff[x].remove(y); debug[vec2(x,-y)]=str("<-"_,x-lastX,y-y2); goto again;
                            } else if(staff[lastX].size()<staff[x].size()) {
                                if(!staff[x].contains(y2)) staff[x].insertSorted(y2,staff[lastX].at(y2));
                                staff[lastX].remove(y2); debug[vec2(lastX,-y2)]=str("->"_,x-lastX,y-y2); goto again;
                            }
                        } else if(abs(x-lastX)<10 || (abs(x-lastX)<20 && abs(y-y2)<20)) debug[vec2(x,-y+16)]="?"_+str(x-lastX,y-y2);
                    }
                }
            }
            lastX=x;
        }
    }

    /// Detect and explicit tremolos
#if 0
    uint begin=0;
    for(uint i: range(tremolos.size())) {
        if(i+1>=tremolos.size() || tremolos[i].a.x!=tremolos[i+1].a.x) {
            int N = i+1-begin;
            for(Line tail : tails) if(abs(tail.a.x-tremolos[i].a.x)<6 && abs(tail.b.y-tremolos[i].a.y)<60) { log("Spurious"); goto spurious; }
            {
                uint staff=0; for(;staff<staffs.size()-1 && tremolos[i].a.y>staffs[staff];staff++) {}
#if 0
                // single chord tremolo
                int X=0; for(int x : notes[staff].keys) if(x>tremolos[i].a.x-10) { X=x; break; }
                debug[vec2(X,notes[staff][X].keys.first())]=string("T"_);
                if(X && N==2) {
                    map<int,Note> chord;
                    for(pair<int,Note> note: notes[staff].at(X)) {
                        if(abs(-note.key-tremolos[i].a.y)<75) chord.insert(note.key, note.value);
                        else if(abs(-note.key-tremolos[i].a.y)<90) debug[vec2(X,-note.key)]="T"_+dec(-note.key-tremolos[i].a.y);
                    }
                    int duration = chord.values.first().duration;
                    int times = duration*(1<<N);
                    for(int t=1;t<=times;t++) notes[staff].insertMulti(X+t*3, copy(chord));
                }
#endif
#if 0 //alternating tremolo
                int X1=0,X2=0,Y1=0,Y2=0;
                for(int x : notes[staff].keys) {
                    for(int y : notes[staff][x].keys) {
                        if(abs(-y-tremolos[i].a.y)<70) goto inRange;
                        else if(abs(-y-tremolos[i].a.y)<120) log(abs(-y-tremolos[i].a.y));
                    }
                    continue;
inRange: ;
                    if(x<tremolos[i].a.x) { X1=x, Y1=notes[staff].at(X1).keys[0]; }
                    if(x+10>tremolos[i].b.x) { X2=x; Y2=notes[staff].at(x).keys[0]; break;}
                }
                debug[tremolos[i].a]=string("A"_);
                debug[tremolos[i].b]=string("B"_);
                debug[vec2(X1,-Y1)]=string("T1"_);
                debug[vec2(X2,-Y2)]=string("T2"_);
                log("Tremolo",begin,i,X1,X2,Y1,Y2);
                if(X1&&X2) {
                    Note a = notes[staff][X1].take(Y1), b = notes[staff][X2].take(Y2);
                    int duration = a.duration+b.duration;
                    int times = duration*(N?4:2)/8;
                    for(int t=0;t<times;t++) {
                        notes[staff][X1+(X2-X1)*(2*t+0)/times].insert(Y1, Note(a.index, duration/times));
                        notes[staff][X1+(X2-X1)*(2*t+1)/times].insert(Y2, Note(a.index, duration/times));
                    }
                }
#endif
            }
spurious: ;
            begin=i;
        }
    }
#endif

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
    int startIndex=-1;
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
    }

    //for(Line l: ties) debug.insertMulti((l.a+l.b)/2.f,string("^"_));
    for(float y: staffs) debug[vec2(0,y-16)]=string("________"_);

    //dots.clear(); notes.clear(); repeats.clear(); ties.clear(); tails.clear(); tremolos.clear(); trills.clear();
}

void Score::synchronize(const map<uint,Chord>& MIDI) {
    /// Synchronize notes to MIDI track
    array<MidiNote> notes; //flatten chords for robust MIDI synchronization
    for(const Chord& chord: MIDI.values) notes<<chord;
    vec2 lastPos=vec2(0,0); int lastKey=0;
    for(uint i=0; i<notes.size() && i<positions.size();) {
        vec2 pos=positions[i]; MidiNote note = notes[i];
        /*if(n<notes.size() && notes[n]==note && lastY && y!=lastY && abs(y-lastY)<16) { //double notes in MIDI
                    debug[vec2(x,-y)]=string("+++"_);
                    chord<<vec2(x,-y); positions<<vec2(x,-y); indices<<staff[x].at(y).index; staff[x].at(y).scoreIndex=n; n++;
                }*/
        /*if(pos.y==lastPos.y && note!=lastNote) { // double notes in score
            debug[pos]=string("////"_);
            positions.removeAt(i); indices.removeAt(i);
        } else*/
        if(lastPos && lastKey && pos.x==lastPos.x && pos.y<lastPos.y && note.key<lastKey) { // missing note in MIDI
            debug[pos]=string("++++"_);
            positions.removeAt(i); indices.removeAt(i);
        } else if(lastPos && lastKey && lastPos.x<=pos.x-82 && pos.y>=lastPos.y+101 && note.key>lastKey) { // spurious note in MIDI
            debug.insertMulti(pos,str("----"_,lastPos.x-pos.x,lastPos.y-pos.y));
            notes.removeAt(i);
        } else {
            debug.insertMulti(positions[i]+vec2(12,0),str(notes[i].key));
            i++;
        }
        lastPos=pos; lastKey=note.key;
    }

    chords.clear();
    uint t=-1; for(uint i: range(min(notes.size(),positions.size()))) { //reconstruct chords after edition
        if(i==0 || positions[i-1].x != positions[i].x) chords.insert(++t);
        chords.at(t) << notes[i];
        debug.insertMulti(positions[i]+vec2(12,0),str(notes[i].key));
    }
}

void Score::annotate(map<uint,Chord>&& chords) {
    /// Synchronize notes to MIDI track
    array<MidiNote> notes; //flatten chords for robust annotations
    for(const Chord& chord: chords.values) notes<<chord;
    for(uint i=0; i<notes.size() && i<positions.size();) {
        debug.insertMulti(positions[i]+vec2(12,0),str(notes[i].key));
        i++;
    }
    this->chords=move(chords);
}

void Score::toggleEdit() {
    editMode=!editMode;
    expected.clear();
    if(editMode) {
        expected.insert(0, noteIndex);
        annotationsChanged(chords); //setAnnotations
    } else {
        debug.clear();
        annotationsChanged(chords); //setAnnotations
    }
    //else seek(time)
    map<int,byte4> activeNotes;
    for(int i: expected.values) activeNotes.insertMulti(indices?indices[i]:i,blue);
    activeNotesChanged(activeNotes);
}

void Score::previous() {
    if(editMode) {
        expected.clear();
        expected.insert(0, --noteIndex);
        map<int,byte4> activeNotes;
        for(int i: expected.values) activeNotes.insertMulti(indices?indices[i]:i,blue);
        activeNotesChanged(activeNotes);
    }
}

void Score::next() {
    if(editMode) {
        expected.clear();
        expected.insert(0, ++noteIndex);
        map<int,byte4> activeNotes;
        for(int i: expected.values) activeNotes.insertMulti(indices?indices[i]:i,blue);
        activeNotesChanged(activeNotes);
    }
}

void Score::insert() {
    if(editMode) {
        assert(expected.size()==1 && expected.values[0]==(int)noteIndex);

        array<MidiNote> notes; //flatten chords for robust synchronization
        uint t=0; for(const Chord& chord: chords.values) { notes<<chord; t++; }
        if(noteIndex<=notes.size()) {
            notes.insertAt(noteIndex,MidiNote __(0,0,0));

            chords.clear(); debug.clear(); //reconstruct chords from PDF
            uint t=-1; for(uint i: range(notes.size())) {
                if(i==0 || positions[i-1].x != positions[i].x) chords.insert(++t);
                chords.at(t) << notes[i];
                debug.insertMulti(positions[i]+vec2(12,0),str(notes[i].key));
            }
            annotationsChanged(chords);
        }

        expected.clear(); expected.insert(0, noteIndex);
        map<int,byte4> activeNotes;
        for(int i: expected.values) activeNotes.insertMulti(indices?indices[i]:i,blue);
        activeNotesChanged(activeNotes);
    }
}

void Score::remove() {
    if(editMode) {
        assert(expected.size()==1 && expected.values[0]==(int)noteIndex);

        array<MidiNote> notes; //flatten chords for robust synchronization
        uint t=0; for(const Chord& chord: chords.values) { notes<<chord; t++; }
        if(noteIndex<=notes.size()) {
            notes.removeAt(noteIndex);

            chords.clear(); debug.clear(); //reconstruct chords from PDF
            uint t=-1; for(uint i: range(notes.size())) {
                if(i==0 || positions[i-1].x != positions[i].x) chords.insert(++t);
                chords.at(t) << notes[i];
                debug.insertMulti(positions[i]+vec2(12,0),str(notes[i].key));
            }
            annotationsChanged(chords);
        }

        expected.clear(); expected.insert(0, noteIndex);
        map<int,byte4> activeNotes;
        for(int i: expected.values) activeNotes.insertMulti(indices?indices[i]:i,blue);
        activeNotesChanged(activeNotes);
    }
}

void Score::seek(uint unused time) {
    if(!staffs) return;
    assert(time==0,"TODO");
    if(editMode) {
        expected.clear();
        expected.insert(0,noteIndex=0);
    } else {
        chordIndex=0, noteIndex=0; currentStaff=0; expected.clear(); active.clear();
        int i=noteIndex; for(MidiNote note: chords.values[chordIndex]) {
            expected.insertMulti(note.key, i);
            while(positions[i].y>staffs[currentStaff] && currentStaff<staffs.size()-1) {
                assert(currentStaff<staffs.size());
                if(currentStaff>0) nextStaff(staffs[currentStaff-1],staffs[currentStaff],staffs[min(staffs.size()-1,currentStaff+1)]);
                currentStaff++;
            }
            i++;
        }
    }
    map<int,byte4> activeNotes;
    for(int i: expected.values) activeNotes.insert(indices?indices[i]:i,blue);
    activeNotesChanged(activeNotes);
}

void Score::noteEvent(int key, int vel) {
    if(editMode) {
        if(vel) {
            assert(expected.size()==1 && expected.values[0]==(int)noteIndex);

            array<MidiNote> notes; //flatten chords for robust synchronization
            uint t=0; for(const Chord& chord: chords.values) { notes<<chord; t++; }
            if(noteIndex<=notes.size()) {
                if(noteIndex==notes.size()) notes << MidiNote __(key, t, 1);
                else notes[noteIndex]=MidiNote __(key, t, 1);

                chords.clear(); debug.clear(); //reconstruct chords from PDF
                uint t=-1; for(uint i: range(notes.size())) {
                    if(i==0 || positions[i-1].x != positions[i].x) chords.insert(++t);
                    chords.at(t) << notes[i];
                    debug.insertMulti(positions[i]+vec2(12,0),str(notes[i].key));
                }
                annotationsChanged(chords);

                expected.clear(); expected.insert(0, ++noteIndex);
            }
        }
    } else {
        if(vel) {
            if(expected.contains(key)) {
                active.insertMulti(key,expected.at(key));
                expected.remove(key);
                if(expected.size()==1 && chordSize>=4) miss=move(expected);
            } else if(miss.contains(key)) miss.remove(key);
            else return;
        } else if(key) {
            if(active.contains(key)) active.remove(key);
            return;
        }
        if(!expected && chordIndex<chords.size()-1) {
            noteIndex+=chords.values[chordIndex].size();
            chordIndex++;
            int i=noteIndex; for(MidiNote note: chords.values[chordIndex]) {
                expected.insertMulti(note.key, i);
                while(positions[i].y>staffs[currentStaff] && currentStaff<staffs.size()-1) {
                    assert(currentStaff<staffs.size());
                    if(currentStaff>0) nextStaff(staffs[currentStaff-1],staffs[currentStaff],staffs[min(staffs.size()-1,currentStaff+1)]);
                    currentStaff++;
                }
                i++;
            }
            chordSize = expected.size();
        }
    }
    map<int,byte4> activeNotes;
    for(int i: expected.values) activeNotes.insertMulti(indices?indices[i]:i,blue);
    for(int i: miss.values) activeNotes.insertMulti(indices?indices[i]:i,blue);
    //for(int i: active.values) if(!activeNotes.contains(indices?indices[i]:i)) activeNotes.insert(indices?indices[i]:i,red);
    activeNotesChanged(activeNotes);
}
