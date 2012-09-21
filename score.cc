#include "score.h"

void Score::onPath(const ref<vec2>& p) {
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
        if(abs(p[0].y-p[3].y)<1) {
            ties+= Line(p[0],p[3]);
        }
    }
}

void Score::onGlyph(int unused index, vec2 pos, float unused size,const ref<byte>& font, int unused code) {
    uint i=0; for(;i<staffs.size() && pos.y>staffs[i];i++) {}
    int duration=-1;
    // Opus is Sibelius default font, Emmentaler is Lilypond default font
    if(find(font,"Opus"_)||find(font,"Emmentaler"_)) {
        if(endsWith(font,"Opus"_)||find(font,"Emmentaler"_)) {
            //TODO: quaver detection
            if(code==53/*Opus*/ || code==1/*Emmentaler*/) {
                if(size<30) duration= 0; //grace
                else duration = 4; //quarter
            }
            else if(code==66) duration = 8; //half
            else if(code==39) duration = 16; //whole
            else if(code==71/*Opus treble*/||code==11/*Opus bass*/||code==214/*Emmentaler C key*/) {
                if(pos.y-lastClef.y>128) staffs << (lastClef.y+90);
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
                if(x>pos.x-48) for(int y : notes[i][x].keys) if(-y>pos.y-16&&-y<pos.y+32) notes[i][x][y].duration *= 3/2;
            }
            dots[i] << pos;
            map<int, vec2> matches;
            for(vec2 dot : dots[i]) if(dot.x==pos.x) matches[dot.y]=dot;
            const array<int>& y = matches.keys; const array<vec2>& m = matches.values;
            if(m.size()==4 && abs(y[0]-y[1])<12 && abs(y[1]-y[2])>100 && abs(y[2]-y[3])<12 ) repeats<<(m[0]+m[1]+m[2]+m[3])/4.f;
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
    if(duration>=0) notes[i][pos.x][-pos.y] = Note(index,duration);
}

struct Tie { uint li; int lx,ly; int ri,rx,ry; Tie():li(0),lx(0),ly(0),ri(0),rx(0),ry(0){}};
void Score::synchronize(array<int> MIDI) {
    staffs << (lastClef.y+96); //add a last split at the bottom of the last page

    /// Detect and remove tied notes
    array<Tie> tied;
    for(Line tie : ties) {
        //debug << Debug(tie.a,"T");
        int l = abs(tie.b.x-tie.a.x);
        uint i=0; for(;i<staffs.size()-1 && tie.a.y>staffs[i];i++) {}
        int noteBetween=0; Tie t;
        for(int x : notes[i].keys) for(int y : notes[i][x].keys) {
            int lx = x-tie.a.x;
            int ly = -y-tie.a.y;
            int rx = x-tie.b.x;
            int ry = abs(y-t.ly);

            /// Detect first note of a tie
            if(!t.ly) if(notes[i][x][y].duration>0/*not grace*/ && lx < 4 && lx > -32 && ly < 9) {
                //debug << Debug(vec2(x,-y),"L");
                for(Tie t2 : tied) if(t2.li==i && t2.lx==x && t2.ly==y) goto alreadyTied;
                t.li=i; t.lx=x; t.ly=y;
            }

            /// Detect if there is a note between the tied notes (necessary to sync with HTTYD sheets)
            if(!noteBetween && lx > 0 && rx < -16 && ry < 7) {
                //debug << Debug(vec2(x,-y),"B");
                noteBetween++;
                if(abs(x-t.lx)<32) noteBetween++;
                break;
            }

            /// Remove every other note between tied notes (necessary to sync with HTTYD sheets)
            if(noteBetween%2 && lx > 0 && rx < -16 && ry < 2 && l < 200) {
                //debug << Debug(vec2(x,-y),"O");
                notes[i][x].remove(y); noteBetween++;
            }

            /// Detect right note of a tie
            if( (!noteBetween || (noteBetween<2 && l<210)) && ry < 6 && rx < 20 && rx > -12) {
                t.ri=i;t.rx=x; t.ry=y;
                tied << t; //defer remove for double ties
                //debug << Debug(vec2(x,-y)," R");
                goto staffDone;
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
                for(int y2 : notes[i+1].values[x].keys) {
                    if(abs((-t.ly-staffs[i])-(-y2-staffs[i+1]))<12) {
                        t.ri=i+1;t.rx=rx; t.ry=y2;
                        //debug << Debug(tie.a,"W");
                        tied << t;
                        break;
                    }
                }
            }
trillCancelTie: ;
        }
    }
    for(Tie t : tied) notes[t.ri][t.rx].remove(t.ry);

    /// Remove muted double notes (necessary to sync with HTTYD sheets)
    for(int i : notes.keys) {
        int lastX=0;
        for(int x : notes[i].keys) {
            if(lastX>0) for(int y : notes[i][x].keys) for(int y2 : notes[i][lastX].keys)
                if(notes[i][lastX][y2].duration && (abs(x-lastX)<=4 || (abs(x-lastX)<18 && y!=y2 && (x-lastX)+(y-y2)<32))){
                    if(notes[i][lastX].size()>=notes[i][x].size()) {
                        notes[i][lastX][y]=notes[i][x][y]; notes[i][x].remove(y); break;
                    } else if(notes[i][lastX].size()<notes[i][x].size()) {
                        notes[i][x][y2]=notes[i][lastX][y2]; notes[i][lastX].remove(y2); break;
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

    /// Synchronize notes to MIDI track
    if(MIDI.size()) {
        map<int,vec2> lastChord; bool lastSync=true;
        uint n=0;
        for(int i : notes.keys) for(int x : notes[i].keys) {
            map<int,vec2> chord; int note=0; bool sync=true;
            map<int,int> noteChord;
            int lastY=0;
            for(int y : notes[i][x].keys) {
                if(!repeats.size()) { //FIXME
                    if(n<MIDI.size() && MIDI[n]==note && lastY && y!=lastY && abs(y-lastY)<16) {
                        chord[n]=vec2(x,-y); noteChord[n]=notes[i][x][y].index; notes[i][x][y].duration=n; n++;
                    }
                    if(n<MIDI.size() && MIDI[n]<=note) { sync=false; }
                    note = n>=MIDI.size() ? 0 : MIDI[n];
                    if(lastSync && sync && notes[i][x].size()<3) for(uint j : lastChord.keys) { if(j>=MIDI.size()) break;
                        if(abs(lastChord[j].y+y)<=1 && abs(MIDI[j]-note)>2 && note!=MIDI[j]+12) {
                            //debug << Debug(QPoint(x,-y),"!MIDI");
                            goto skip;
                        }
                    }
                }
                chord[n]=vec2(x,-y); noteChord[n]=notes[i][x][y].index; notes[i][x][y].duration=n; n++; lastY=y;
skip: ;
            }
            positions << chord.values;
            //noteIndices << noteChord.values;
            lastChord=move(chord); lastSync=sync;
        }
    }

    /// Detect and explicit repeats
    int startIndex=-1;
    for(vec2 pos : repeats) {
        uint i=0; for(;i<staffs.size()-1 && pos.y>staffs[i];i++) {}
        int index=notes[i].values[0].values[0].index-1;
        for(int x : notes[i].keys) { if(x>pos.x) break; index=notes[i][x].values[0].index; }
        if(startIndex < 0) startIndex=index;
        else {
            array<vec2> cat; cat<<positions.slice(0,index+1)<<positions.slice(startIndex+1); positions = move(cat);
            //noteIndices = noteIndices.slice(0,index+1)+noteIndices.slice(startIndex+1);
            startIndex=-1; }
    }
    //dots.clear(); notes.clear(); repeats.clear(); ties.clear(); tails.clear(); tremolos.clear(); trills.clear();

#if 0
    vec2 scale(1/recognitionScale,-1/recognitionScale);
    Font font;
    FT_New_Face(ft,"/usr/share/fonts/dejavu/DejaVuSans.ttf",0,&font.face);
    Cm = mat32();
    for(int i=0;i<notes.size;i++) {
        Tm = mat32(scale*notes[i]);
        drawText(&font,9,0,0,string::number(MIDI.value(i)));
    }
    foreach(Line l, ties) { lines << scale*l.a << scale*l.b; }
    foreach(float y, staffs) { lines << scale*vec2(0,y) << scale*vec2(1000,y); }
    foreach(Debug d,debug) {
        Tm = mat32(scale*d.pos);
        drawText(&font,9,0,0,d.text);
    }
#endif
}
