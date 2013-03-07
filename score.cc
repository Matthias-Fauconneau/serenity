#include "score.h"

void Score::onPath(const ref<vec2>& p) {
    vec2 min=p[0], max=p[0]; for(vec2 a: p) min=::min(min,a), max=::max(max,a);
    vec2 center = (min+max)/2.f; vec2 span=max-min;
    if(span.y==0 && span.x>10 && span.x<30) ledgers << center;
    else if(span.y==0 && span.x>100) staffLines << center.y;
    if(p.size==2) {
        if(p[0].x==p[1].x && abs(p[0].y-p[1].y)>20 && abs(p[0].y-p[1].y)<70) {
            tails << Line(p[0], p[1]);
        }
    } else if(p.size==5||p.size==13) {
        if(span.x > 75 && span.x < 76 && span.y > 8 && span.y < 18) {
            tremolos << Line(p[0], p[3]);
        }
    } else if((p.size==4&&p[1]!=p[2]&&p[2]!=p[3])||p.size==7) {
        if(span.y>2 && span.x<370 && (span.y<14 || (span.x>90 && span.y<17) || (span.x>100 && span.y<22) || (span.x>200 && span.y<27) || (span.x>300 && span.y<30) || (span.x>360 && span.y<36))) { //FIXME
            ties.appendOnce( Line(vec2(min.x,p[0].y),vec2(max.x,p[3].y)) );
            //debug[center]="V"_+str(span);
        } //else { debug[center]="!V"_+str(span); log(span); }
    } else if(p.size==10) {
        if(span.x>36 && span.x<1000 && span.y>10 && (span.y<14 || (span.x>100 && span.y<29))) {
            ties.appendOnce( Line(vec2(min.x,center.y),vec2(max.x,center.y)) );
            //debug[center]="X"_+str(span);
        } //else debug[center]="!X"_+str(span);
    }
}

void Score::onGlyph(int index, vec2 pos, float size,const ref<byte>& font, int code, int fontIndex) {
    if(index == 0) {
        pass++;
        if(histogram) { //FIXME: OCR
            map<int, int> sorted;
            for(pair<int,int> sample: histogram) if(!sorted.contains(sample.value)) sorted.insertSorted(sample.value, sample.key); //insertion sort
            quarter = sorted.values.last();
            if(sorted.values[sorted.values.size()-3]==9) half = 9;
            else half = sorted.values[sorted.values.size()-4];
            //log(quarter, half, sorted);
        }
    }
    //TODO: OCR glyphs and factorize logic
    if(pass==0) { // 1st pass: split score in staves
        //FIXME: (lastClef.y+pos.y)/2 breaks wrapped ties, use lastClef.y+110 if pos.y-lastClef.y>130, or switch to a better staff detection method
        if(font=="MScore-20"_) { //TODO: glyph OCR
            if((code==1||code==12/*treble*/||code==2||code==13/*bass*/) && pos.x<200) {
                if(pos.y-lastClef.y>170) staffs << (lastClef.y+pos.y)/2;
                if(pos.y>lastClef.y) lastClef=pos;
                if(code==12 || code==13) msScore=1;
            }
        } else if(find(font,"LilyPond"_)) {
          if((code==147/*treble*/||code==145/*bass*/) && pos.x<200) {
                if(pos.y-lastClef.y>201) staffs << (lastClef.y+pos.y)/2;
                //else { static uint max=0; int y=pos.y-lastClef.y; if(uint(y)>max) max=y, log(pos.y-lastClef.y); }
                if(pos.y>lastClef.y) lastClef=pos;
            }
        } else if(font=="OpusStd"_) {
            if((code==3||code==6 ||code==5 /*||code==7*/) && pos.x<200) { //FIXME: OCR
                if(pos.y-lastClef.y>148 && staffCount!=1) {
                    log(pos.y-lastClef.y);
                    if(pos.y-lastClef.y>200) staffs << lastClef.y+110; // for ties wrapped on staff before page breaks
                    else staffs << (lastClef.y+pos.y)/2+12;
                    staffCount=1;
                } else staffCount++;
                lastClef=pos;
            }
            histogram[code]++;
        } else if(endsWith(font,"Opus"_)) {
            if((fontIndex==71/*treble*/||fontIndex==11/*bass*/) && pos.x<200) {
                if(pos.y-lastClef.y>148 && staffCount!=1) {
                    if(pos.y-lastClef.y>200) staffs << lastClef.y+110; // for ties wrapped on staff before page breaks
                    else staffs << (lastClef.y+pos.y)/2; //+14 //-6; //-18; FIXME?
                    staffCount=1;
                } else staffCount++;
                lastClef=pos;
            }
         } else if(find(font,"DUCRGK"_)) { //TODO: glyph OCR
            if(code==1/*treble*/||code==5/*bass*/) {
                if(lastClef.y != 0 && pos.y-lastClef.y>128) staffs << (lastClef.y+110);
                lastClef=pos;
            }
        } else if(find(font,"ZVBUUH"_)||find(font,"JDAHFL"_)) { //TODO: glyph OCR
            if(code==1/*treble*/||code==2/*bass*/) {
                if(lastClef.y != 0 && pos.y-lastClef.y>159) staffs << (lastClef.y+pos.y)/2;
                lastClef=pos;
            }
        } else if(endsWith(font,"Inkpen2"_)) { //TODO: glyph OCR
            if(fontIndex==5/*treble*/||fontIndex==23/*bass*/) {
                if(lastClef.y != 0 && pos.y-lastClef.y>128) staffs << (lastClef.y+pos.y)/2;
                lastClef=pos;
            }
        } else if(find(font,"NWCV15"_)) { //TODO: glyph OCR
            if(code==97/*treble*/||code==98/*bass*/) {
                if(lastClef.y != 0 && pos.y-lastClef.y>128) staffs << (lastClef.y+pos.y)/2;
                lastClef=pos;
            }
        } else if(font=="Manual"_) { // Manual annotations
            if(!staffs || (pos.x < 300 && lastPos.x > 900 && pos.y > lastPos.y)) { staffs << lastClef.y+70; lastClef=pos; }
            lastPos=pos;
            uint i=0; for(;i<staffs.size() && pos.y>staffs[i];i++) {}
            debug[pos]=str(i);
            if(i>=notes.size()) notes.grow(i+1);
            for(int x : notes[i].keys) if(abs(x-pos.x)<16) { notes[i].sorted(x).insertSorted(-pos.y, Note(index,4)); goto break_; }
            /*else*/ notes[i].sorted(pos.x).insertSorted(-pos.y, Note(index,4));
            break_:;
        }
    } else if(pass==1) { // 2nd pass: detect notes and assign to staves
        //log(font);
        //debug[pos]=str(code,fontIndex);
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
            if(font=="OpusStd"_) { //FIXME: OCR
                if(code == quarter /*code==7*//*code==8*/) {
                    if(size<30) duration= 0; //grace
                    else duration = 4; //quarter
                }
                else if(code == half/*code == 9/10/11*/) duration = 8; //half
                else if(code==16) duration = 16; //whole
            } else if(endsWith(font,"Opus"_)) {
                if(fontIndex==53) {
                    if(size<30) duration= 0; //grace
                    else duration = 4; //quarter
                }
                else if(fontIndex==66) duration = 8; //half
                else if(fontIndex==39) duration = 16; //whole
            }
            if(code==41 && trills && abs(trills.last().b.y-pos.y)<16) { trills.last().b=pos; debug[pos]=string("Trill"_); } //trill tail
            else if(code==56) { trills << Line(pos,pos); debug[pos]=string("Trill"_); } //trill head
            else if(code==58 || (font=="OpusSpecialStd"_ && code==1)) { //dot
                dots[i] << pos;
                map<float, vec2> matches;
                for(vec2 dot : dots[i]) if(abs(dot.x-pos.x)<1) matches[dot.y]=dot;
                const array<float>& y = matches.keys; const array<vec2>& m = matches.values;
                if(m.size()==4 && abs(y[0]-y[1])<15 && abs(y[1]-y[2])>121 && abs(y[2]-y[3])<15 ) {
                    vec2 pos = (m[0]+m[1]+m[2]+m[3])/4.f;
                    uint i=0; for(;i<repeats.size() && repeats[i].y*1000+repeats[i].x < pos.y*1000+pos.x;i++) {} repeats.insertAt(i,pos);
                    debug[pos]=str(":"_,i);
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
        }  else if(find(font,"JDAHFL"_)) { //TODO: glyph OCR
            if(code==7) {
                if(size<2) duration = 0; //grace
                else duration = 4; //quarter
            }
            else if(code==13) duration = 8; //half
        }
        else if(endsWith(font,"Inkpen2"_)) { //TODO: glyph OCR
            if(fontIndex==65) {
                if(size<2) duration = 0; //grace
                else duration = 4; //quarter
            }
            else if(fontIndex==78) duration = 8; //half
            else if(fontIndex==51) duration = 16; //whole
        } else if(find(font,"NWCV15"_)) { //TODO: glyph OCR
            if(code==107) {
                if(size<2) duration = 0; //grace
                else duration = 4; //quarter
            }
            else if(code==106) duration = 8; //half
            else if(code==105) duration = 16; //whole
        }
        if(duration<0) return;
        if(notes[i].sorted(pos.x).contains(-pos.y)) return;
        if(staffs) {
            float nearestStaffCut = min(abs(pos.y-staffs[max(int(i)-1,0)]),abs(pos.y-staffs[min(i,staffs.size()-1)]));
            if(nearestStaffCut<20) { // Follow ledgers away from staff limit
                float max=0; vec2 best=pos;
                for(vec2 ledger: ledgers) {
                    float d = length(ledger-pos);
                    if(abs(ledger-pos).y>max && d<30) {
                        max=abs(ledger-pos).y, best=ledger;
                    }
                }
                i=0; for(;i<staffs.size() && best.y>staffs[i];i++) {}
                if(i>=notes.size()) notes.grow(i+1);
                float nearestStaffCut = ::min(abs(best.y-staffs[::max(int(i)-1,0)]),abs(best.y-staffs[i]));
                if(nearestStaffCut<20) { // Follow staff lines away from staff limit
                    float min=30; vec2 best2=best;
                    for(float y: staffLines) {
                        float d = abs(y-best.y);
                        if((abs(y-pos.y)<min && abs(y-pos.y)<10) || d<min) {
                            min=d, best2.y=y;
                        }
                    }
                    i=0; for(;i<staffs.size() && best2.y>staffs[i];i++) {}
                    if(i>=notes.size()) notes.grow(i+1);
                }
            }
        }
        notes[i].sorted(pos.x).insertSorted(-pos.y, Note(index,duration));
    }
}

struct Tie { uint li; int lx,ly; uint ri; int rx,ry; int dy; Tie():li(0),lx(0),ly(0),ri(0),rx(0),ry(0){}};
string str(const Tie& t) { return "Tie("_+str(t.li,t.lx,t.ly,"-",t.ri,t.rx,t.ry)+")"_; }
void Score::parse() {
    if(!staffs) return; assert(staffs);
    staffs << (lastClef.y+110); //add a last split at the bottom of the last page

    /// Lengthens dotted notes
    for(pair< int,array<vec2> > dots: this->dots) {
        for(vec2 pos: dots.value) for(int x : notes[dots.key].keys) {
            if(x>pos.x-16) break;
            if(x>pos.x-48) for(int y : notes[dots.key][x].keys) if(-y>pos.y-16&&-y<pos.y+32) notes[dots.key][x].at(y).duration = notes[dots.key][x].at(y).duration*3/2;
        }
    }

    /// Fix chords with diadics (shifted x positions) or double notes
    for(map<int, map< int, Note> >& staff : notes) {
        for(uint i: range(staff.keys.size())) {
            if(i>0) {
                int pX = staff.keys[i-1]; map< int, Note>& lastChord = staff.values[i-1];
                int x = staff.keys[i]; map< int, Note>& chord = staff.values[i];
                int lastD=0; for(const Note& note: lastChord.values) lastD=max(lastD, note.duration);
                again: ;
                for(int y: chord.keys) {
                    for(int pY : lastChord.keys) {
                        if(lastChord.at(pY).duration && (
                                    abs(x-pX)<2 ||
                                    //(abs(x-pX)<=22 && y==pY && chord.size()==1) || //cancel repeated note in diadic
                                    (abs(x-pX)<10 && abs(y-pY)<180 && (y!=pY || lastChord.size()>1 || chord.size()>1)) ||
                                    ((abs(x-pX)<18 && abs(y-pY)<=36) && (y!=pY || lastChord.size()>1 || chord.size()>1)) ||
                                    ((abs(x-pX)<=19 && abs(y-pY)<=7) && (y!=pY)) ||
                                    (lastD>=16 && (abs(x-pX)<=26 && abs(y-pY)<=18) && (y!=pY || lastChord.size()>1 || chord.size()>1))
                                    )) {
                            if(i<staff.keys.size()-1 && abs(staff.keys[i+1]/*nextX*/ - x) <= abs(x-pX)) { //prevent stealing diadic from wrong chord
                                for(int nY : staff.values[i+1].keys) if(abs(nY-y)<=abs(x-pX)) goto skip;
                            }
                            if((lastChord.size()>=chord.size() || (lastChord.size()==3 && chord.size()==4)) && ( //FIXME
                                          (abs(x-pX)<18 && abs(y-pY)<=36) ||
                                          (chord.size()==1 && abs(x-pX)<=22 && abs(y-pY)<=0) ||
                                    (lastD>=16 && (abs(x-pX)<=26 && abs(y-pY)<=18) && (y!=pY || lastChord.size()>1 || chord.size()>1))
                                          )) {
                                /*if(!lastChord.contains(y))*/ lastChord.insertSortedMulti(y,chord.at(y)); //tie only one duplicate
                                chord.remove(y); debug[vec2(x,-y)]<<str("<"_,x-pX,y-pY,lastD); goto again;
                            } else if(lastChord.size()<=chord.size() && !(lastChord.size()==3 && chord.size()==4)/*FIXME*/) {
                                if(!chord.contains(pY)) chord.insertSorted(pY,lastChord.at(pY));
                                lastChord.remove(pY); debug[vec2(pX,-pY)]<<str(">"_,abs(x-pX),abs(y-pY)); goto again;
                            } //else debug[vec2(x,-y)]<<str("?"_,x-pX,y-pY);
                        } //else if(abs(x-pX)<26 && abs(y-pY)<40) debug[vec2(x,-y)]<<"!"_+str(x-pX,y-pY);
                        goto ok; //FIXME
                        skip:;
                        //debug[vec2(x,-y)]<<"!"_+str(x-pX,y-pY);
                        ok:;
                    }
                }
            }
        }
    }

    /// Detect and remove tied notes
    array<Tie> tied;
    for(Line tie : ties) {
        int l = abs(tie.b.x-tie.a.x);
        uint staff=0; for(;staff<staffs.size()-1 && tie.a.y>staffs[staff];staff++) {}
        int noteBetween=0; Tie t;
        for(uint i=staff>0?staff-1:0;i<staff+1;i++) {
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
                        } //else if(lx>-49 && lx<4 && ly>-40 && ly<20 && rx<1) debug[vec2(x,-y)]<<string("!L"_+str(lx,ly,rx));
                    }
alreadyTied: ;
                }
                for(int y : notes[i][x].keys) {
                    int ry = y-t.ly;
                    /// Detect if there is a note between the tied notes (necessary to sync with HTTYD sheets)
                    if(lx > 0 && rx < -16 && abs(ry) < 5) {
                        debug[vec2(x,-y)]=str("B"_,ry);
                        //if(noteBetween && (noteBetween==2 || notes[t.li][t.lx].at(t.ly).duration==8)) notes[i][x].remove(y);
                        if(notes[t.li][t.lx].at(t.ly).duration>=8 && noteBetween==1) notes[i][x].remove(y);
                        noteBetween++; //if(abs(x-t.lx)<32) noteBetween++;
                        break;
                    }
                    /// Detect right note of a tie
                    if( (!noteBetween || (noteBetween<2 && l>210)) && ry>/*=*/-6 && ry < 7 && rx < 21 && rx > -10/*-9*//*-12*/) {
                        t.ri=i;t.rx=x; t.ry=y;
                        tied << t; //defer remove for double ties
                        debug[vec2(x,-y)]=string("R"_+str(rx,ry));
                        //if(noteBetween) debug[vec2(x,-y)]=str("B"_,l,ry);
                        goto staffDone;
                    } else if(rx>-40 && rx<40 && ry>-40 && ry<40) debug[vec2(x,-y)]<<str("!R"_,rx,ry,l);
                }
            }
staffDone: ;
            /// Detect notes tied over a line wrap
            if(t.ly && (!noteBetween || (noteBetween<2 && l>150)) && i+1<staffs.size() && tie.b.x > notes[i].keys.last()+10 ) {
                debug[tie.a]=string("W"_);
                float ly = -t.ly-staffs[i]; //+14?
                debug[vec2(notes[i+1].keys.first(),staffs[i+1]+ly)]<<string("R"_);
                for(int x=0;x<1;x++) {
                    int rx = notes[i+1].keys[x];
                    int ry = notes[i+1].values[x].keys[0];
                    for(Line trill : trills) if(abs(rx-trill.a.x)<8 && -ry-trill.a.y>0 && -ry-trill.a.y<200) goto trillCancelTie;
                    float min=15/*14*/;
                    for(float y2 : notes[i+1].values[x].keys) {
                        float dy = (-y2-staffs[i+1])-ly;
                        for(Tie o: tied) if(t.ri == o.ri && t.rx == o.rx && t.ry==o.ry) goto alreadyTied1;
                        if(dy>=0) min=::min(min, abs(dy));
alreadyTied1: ;
                    }
                    for(float y2 : notes[i+1].values[x].keys) {
                        float dy = (-y2-staffs[i+1])-ly;
                        if(dy>=-15/*12*/ && abs(dy)<=min) {
                            t.ri=i+1;t.rx=rx; t.ry=y2;
                            debug[vec2(rx,-y2)]<<str("W"_,dy);
                            for(Tie o: tied) if(t.ri == o.ri && t.rx == o.rx && t.ry==o.ry)
                                //error(-t.ly-staffs[i]-(-y2-staffs[i+1]), -o.ly-staffs[i]-(-y2-staffs[i+1]));
                                goto alreadyTied2;
                            tied << t;
                            goto tieFound;
                        } else if(abs(ly-(-y2-staffs[i+1]))<100) debug[vec2(rx,-y2)]<<"Y"_+str(dy,min);
alreadyTied2: ;
                    }
                }
tieFound: ;
trillCancelTie: ;
            } //else debug[tie.a]=str("!W"_,noteBetween,tie.b.x,notes[i].keys.last()+10);
        }
    }
    for(Tie t : tied) if(notes[t.ri][t.rx].contains(t.ry)) notes[t.ri][t.rx].remove(t.ry);

    /// Removes duplicates (added to tie only once)
    for(Staff& staff: notes) {
        for(uint i: range(staff.keys.size())) {
            map< int, Note>& chord = staff.values[i];
            for(uint i=0;i<chord.size();) {
                for(uint j=0;j<chord.size();j++) {
                    if(i!=j && chord.keys[i]==chord.keys[j]) { chord.keys.removeAt(i), chord.values.removeAt(i); goto continue2; }
                }
                i++; continue2:;
            }
        }
    }

    /// Flatten sorted notes
    uint i=0; for(Staff& staff: notes) {
        for(int x : staff.keys) for(int y : staff.at(x).keys) {
            staff.at(x).at(y).scoreIndex=indices.size();
            positions<<vec2(x,-y); indices<<staff.at(x).at(y).index; durations<<staff.at(x).at(y).duration;
            //debug[positions.last()]<<str(i)+" "_;
        }
        i++;
    }

    /// Detect and explicit repeats
    int startIndex=-2;
    for(vec2 pos : repeats) {
        uint i=0; for(;i<staffs.size()-1 && pos.y>staffs[i];i++) {}
        if(!notes[i].values) { error("Empty staff?"); continue; }
        int index=notes[i].values[0].values[0].scoreIndex-1;
        for(int x : notes[i].keys) { if(x>pos.x) break; index=notes[i][x].values[0].scoreIndex; }
        if(startIndex < -1) {
            startIndex=index; debug[pos]=dec(startIndex)+"{"_;
        } else {
            //if(index>startIndex) { log("index > startIndex",index,startIndex); continue; }
            { array<vec2> cat; cat<<positions.slice(0,index+1)<<positions.slice(startIndex+1); positions = move(cat); }
            { array<int> cat; cat<<indices.slice(0,index+1)<<indices.slice(startIndex+1); indices = move(cat); }
            { array<int> cat; cat<<durations.slice(0,index+1)<<durations.slice(startIndex+1); durations = move(cat); }
            startIndex=-2;
            debug[pos]="}"_+dec(index);
        }
    }

    for(int i: range(staffs.size())) debug[vec2(0,staffs[i]-16)]=str(staffs[i]-staffs[max(0,i-1)],"________"_);
}

void Score::synchronize(const map<uint,Chord>& MIDI) {
    /// Synchronize notes to MIDI track
    array<MidiNote> notes; //flatten chords for robust MIDI synchronization
    for(const Chord& chord: MIDI.values) notes<<chord;

#if 0
    // Removes graces both in MIDI and score (separately as they are not ordered correctly)
    for(uint i=0; i<notes.size();) if(notes[i].duration==0) notes.removeAt(i); else i++;
    for(uint i=0; i<durations.size();) if(durations[i]==0)  positions.removeAt(i), indices.removeAt(i), durations.removeAt(i); else i++;

    // Synchronize score with MIDI
    vec2 lastPos=vec2(0,0); int lastKey=0;
    for(uint i=0; i<notes.size() && i<positions.size();) {
        vec2 pos=positions[i]; MidiNote note = notes[i];
        if(lastPos && lastKey && pos.x==lastPos.x && pos.y<lastPos.y && note.key<lastKey) { // missing note in MIDI
            debug[pos]=string("++++"_);
            positions.removeAt(i); indices.removeAt(i);
        } else i++;
        lastPos=pos; lastKey=note.key;
    }
#endif

    chords.clear();
    uint t=-1; for(uint i: range(min(notes.size(),positions.size()))) { //reconstruct chords after edition
        if(i==0 || positions[i-1].x != positions[i].x) chords.insert(++t);
        chords.at(t) << notes[i];
        debug[positions[i]]<<str(notes[i].key); //+" "_+str((uint)notes[i].duration);
    }
}

/// Show manual note annotations
void Score::annotate(map<uint,Chord>&& chords) {
    array<MidiNote> notes; //flatten chords for robust annotations
    for(const Chord& chord: chords.values) notes<<chord;
    for(uint i=0; i<notes.size() && i<durations.size();) {
        notes[i].duration = durations[i];
        i++;
    }
    this->chords.clear();
    uint t=-1; for(uint i: range(min(notes.size(),positions.size()))) { //reconstruct chords after edition
        if(i==0 || positions[i-1].x != positions[i].x) this->chords.insert(++t);
        this->chords.at(t) << notes[i];
        debug[positions[i]]<<str(notes[i].key);
    }
}

void Score::toggleEdit() {
    editMode=!editMode;
    showExpected = editMode;
    expected.clear();
    if(editMode) {
        expected.insert(0, noteIndex);
        annotationsChanged(chords); //setAnnotations
    } else {
        debug.clear();
        annotationsChanged(chords); //setAnnotations
    }
    //else seek(time)
    map<int,vec4> activeNotes;
    for(int i: expected.values) activeNotes.insertMulti(indices?indices[i]:i,blue);
    activeNotesChanged(activeNotes);
}

void Score::previous() {
    if(editMode && noteIndex>0) {
        expected.clear();
        expected.insert(0, --noteIndex);
        map<int,vec4> activeNotes;
        for(int i: expected.values) activeNotes.insertMulti(indices?indices[i]:i,blue);
        activeNotesChanged(activeNotes);
    }
}

void Score::next() {
    if(editMode) {
        expected.clear();
        expected.insert(0, ++noteIndex);
        map<int,vec4> activeNotes;
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
                debug[positions[i]]<<str(notes[i].key);
            }
            annotationsChanged(chords);
        }

        expected.clear(); expected.insert(0, noteIndex);
        map<int,vec4> activeNotes;
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
        map<int,vec4> activeNotes;
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
    } else if(chords) {
        chordIndex=0, noteIndex=0; currentStaff=0; expected.clear(); active.clear();
        nextStaff(0,0,0);
        int i=noteIndex; for(MidiNote note: chords.values[chordIndex]) {
            if(note.duration > 0 && //skip graces
                    !expected.contains(note.key) //skip double notes
                    ) expected.insert(note.key, i);
            while(positions[i].y>staffs[currentStaff] && currentStaff<staffs.size()-1) {
                assert(currentStaff<staffs.size());
                nextStaff(staffs[currentStaff],staffs[currentStaff],staffs[min(staffs.size()-1,currentStaff+2)]);
                currentStaff++;
            }
            i++;
        }
    }
    if(!showActive) {
        map<int,vec4> activeNotes;
        for(int i: expected.values) activeNotes.insert(indices?indices[i]:i,blue);
        activeNotesChanged(activeNotes);
    }
}

void Score::noteEvent(uint key, uint vel) {
    map<int,vec4> activeNotes;
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
                    debug[positions[i]]<<str(notes[i].key);
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
            }
            else if(!showExpected) showExpected = true; // Only shows expected notes on errors
            else return; // no changes
        } else if(key) {
            if(active.contains(key)) while(active.contains(key)) active.remove(key);
            else return;
        }
        while(!expected && chordIndex<chords.size()-1) {
            noteIndex+=chords.values[chordIndex].size();
            chordIndex++;
            int i=noteIndex; for(MidiNote note: chords.values[chordIndex]) {
                if(note.duration > 0  /*skip graces*/ && !expected.contains(note.key) /*skip double notes*/ ) {
                    expected.insert(note.key, i);
                    showExpected = false; // Hide highlighting while succeeding (otherwise the distraction limits eye-hand span)
                }
                while(positions[i].y>staffs[currentStaff] && currentStaff<staffs.size()-1) {
                    assert(currentStaff<staffs.size());
                    if(currentStaff>0) nextStaff(staffs[currentStaff-1],staffs[currentStaff],staffs[min(staffs.size()-1,currentStaff+2)]);
                    currentStaff++;
                }
                i++;
            }
            chordSize = expected.size();
        }
    }
    if(showActive) for(int i: active.values) if(!activeNotes.contains(indices?indices[i]:i)) activeNotes.insert(indices?indices[i]:i,red);
    if(showExpected) for(int i: expected.values) if(!activeNotes.contains(indices?indices[i]:i)) activeNotes.insertMulti(indices?indices[i]:i,blue);
    activeNotesChanged(activeNotes);
}
