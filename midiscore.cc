#include "midiscore.h"
#include "display.h"

void MidiScore::parse(map<uint,Chord>&& notes, int unused key, uint tempo, uint timeSignature[2], uint ticksPerBeat) {
        this->notes=move(notes);
        this->key=key;
        this->tempo=tempo;
        if(this->timeSignature[0]==3 && this->timeSignature[0]==4) {
            this->timeSignature[0]=timeSignature[0];
            this->timeSignature[1]=timeSignature[1];
        }
        this->ticksPerBeat=ticksPerBeat;
        beatsPerMeasure = this->timeSignature[0]*this->timeSignature[1];
        staffTime = measuresPerStaff*beatsPerMeasure;
    }

int2 MidiScore::sizeHint() { return int2(1280,systemHeight*(notes.keys.last()/staffTime+1)); }

// Returns staff coordinates from note  (for a given clef and key)
int MidiScore::staffY(Clef clef, int note) {
    assert(key==-1 || key==0 || key==2, key);
    int h=note/12*7; for(int i=0;i<note%12;i++) h+=keys[key+1][i];
    const int trebleOffset = 6*7+3; // C0 position in intervals from top line
    const int bassOffset = 4*7+5; // C0 position in intervals from top line
    int clefOffset = clef==Treble ? trebleOffset : clef==Bass ? bassOffset : 0;
    return clefOffset-h;
}

// Returns staff X position from time
int MidiScore::staffX(int t) { return systemHeader+t%staffTime*(size.x-systemHeader-16)/staffTime; }

// Returns page coordinates from staff coordinates
int2 MidiScore::page(int staff, int t, int h) { return position+int2(staffX(t),t/staffTime*systemHeight+(!staff)*staffHeight+2*staffMargin+h*staffInterval/2); }

static void glyph(int2 position, const string name, vec4 color=black) {
    //static Font font{"/usr/share/lilypond/2.16.1/fonts/otf/emmentaler-20.otf"_,128};
    static Font font{"/Sheets/emmentaler-20.otf"_,128};
    const Glyph& glyph = font.glyph(font.index(name));
    blit(position+glyph.offset,glyph.image,color);
}

// Draws a staff
void MidiScore::drawStaff(int t, int staff, Clef clef) {
    for(int i: range(5)) {
        int y = page(staff, t, i*2).y;
        line(int2(position.x+16, y), int2(position.x+size.x-16, y));
    }
    if(clef==Treble) glyph(int2(position.x+16+8,page(staff, t, 3*2).y),"clefs.G"_);
    if(clef==Bass) glyph(int2(position.x+16+8,page(staff, t, 1*2).y),"clefs.F"_);
    for(int i: range(abs(key))) {
        int tone = i*5; if(key<0) tone+=11;
        glyph(int2(position.x+16+48,page(staff,t,staffY(clef,tone)%7).y),key<0?"accidentals.flat"_:"accidentals.sharp"_);
    }
    static constexpr string numbers[10] = {"zero"_,"one"_,"two"_,"three"_,"four"_,"five"_,"six"_,"seven"_,"eight"_,"nine"_};
    glyph(int2(position.x+16+64,page(staff, t, 2*2).y-1),numbers[timeSignature[0]]);
    glyph(int2(position.x+16+64,page(staff, t, 4*2).y-1),numbers[timeSignature[1]]);
}
// Draws a ledger line
void MidiScore::drawLedger(int staff, int t, int h) {
    int2 p = page(staff, t, h);
    line(p+int2(-4,0),p+int2(16,0));
}

void MidiScore::render(int2 position, int2 size) {
    staffs.clear(); positions.clear();
    int lastSystem=-1; uint lastMeasure=0, noteIndex=0;
    this->position=position, this->size=size;
    //Text(str(notes)).render(position,size);
    array<MidiNote> actives[2]; //0 = treble (right hand), 1 = bass (left hand)
    array<MidiNote> quavers[2]; // for quaver linking
    for(uint i: range(notes.size())) {
        uint t = notes.keys[i]*4/ticksPerBeat;

        // Removes released notes from active sets
        for(array<MidiNote>& active: actives) active.filter([t](const MidiNote& note){return note.start+note.duration<=t;});
        uint sustain[2] = { (uint)actives[0].size, (uint)actives[1].size }; // Remaining notes kept sustained

        if(int(t/staffTime)>lastSystem) { // Draws system
            lastSystem=t/staffTime;
            drawStaff(t, 0, Bass);
            drawStaff(t, 1, Treble);
            line(int2(position.x+16,page(1,t,0).y),int2(position.x+16,page(0,t,8).y));
            line(int2(position.x+size.x-16,page(1,t,0).y),int2(position.x+size.x-16,page(0,t,8).y));
            staffs << staffMargin+(lastSystem+1)*systemHeight;
            lastMeasure=t/beatsPerMeasure;
        } else if(t/beatsPerMeasure>lastMeasure) { // Draws measure bars
            line(page(1,t,0)-int2(8,0),page(0,t,8)-int2(8,0));
            lastMeasure=t/beatsPerMeasure;
        }

        array<MidiNote> currents[2]; // new notes to be pressed
        for(MidiNote note: notes.values[i]) { //first rough split based on pitch
            int s = note.key>=60; //middle C
            currents[s] << note;
            actives[s] << note;
        }
        for(uint s: range(2)) { // then balances load on both hand
            array<MidiNote>& active = actives[s];
            array<MidiNote>& otherActive = actives[!s];
            array<MidiNote>& current = currents[s];
            array<MidiNote>& other = currents[!s];
            while(
                  current && // any notes to move ?
                  ((s==0 && current.last().key>=52) || (s==1 && current.first().key<68)) && // prevent stealing from far notes (TODO: relative to last active)
                  current.size>=other.size && // keep new notes balanced
                  active.size>=otherActive.size && // keep active (sustain+new) notes balanced
                  (!other ||
                   (s==1 && abs(int(other.first().key-current.first().key))<=12) || // keep short span on new notes (left)
                   (s==0 && abs(int(other.last().key-current.last().key))<=12) ) && // keep short span on new notes (right)
                  (!sustain[!s] ||
                   (s==1 && abs(int(otherActive[0].key-current.first().key))<=18) || // keep short span with active notes (left)
                   (s==0 && abs(int(otherActive[sustain[!s]-1].key-current.last().key))<=18) ) && // keep short span with active notes (right)
                  (
                      active.size>otherActive.size+1 || // balance active notes
                      current.size>other.size+1 || // balance load
                      // both new notes and active notes load are balanced
                      (currents[0] && currents[1] && s == 0 && abs(int(currents[1].first().key-currents[1].last().key))<abs(int(currents[0].first().key-currents[0].last().key))) || // minimize left span
                      (currents[0] && currents[1] && s == 1 && abs(int(currents[0].first().key-currents[0].last().key))<abs(int(currents[1].first().key-currents[1].last().key))) || // minimize right span
                      (sustain[s] && sustain[!s] && active[sustain[s]-1].start>otherActive[sustain[!s]-1].start) // load least recently used hand
                      )) {
                if(!s) {
                    other.insertAt(0, current.pop());
                    actives[!s].insertAt(0, active.pop());
                } else {
                    other << current.take(0);
                    actives[!s] << active.take(sustain[s]);
                }
            }
        }

        for(uint s: range(2)) { // finally displays notes on the right staff
            Clef clef = (Clef)s;
            int tailMin=100, tailMax=-100; uint minDuration=-1,maxDuration=0;
            for(MidiNote note: currents[s]) { // draws notes
                int h = staffY(clef, note.key);
                for(int i=-2;i>=h;i-=2) drawLedger(s, t, i);
                for(int i=10;i<=h;i+=2) drawLedger(s, t, i);
                int2 position = page(s, t, h);
                uint duration=note.duration;
                if(duration <= 12) {
                    glyph(position, duration<=6?"noteheads.s2"_:"noteheads.s1"_, colors.value(noteIndex,black));
                    int accidental = accidentals[key+1][note.key%12];
                    if(accidental) glyph(position+int2(-12,0),accidental==1?"accidentals.flat"_:"accidentals.sharp"_);
                    if(duration<=3) quavers[s] << note;
                    else {
                        tailMin=min(tailMin,h), tailMax=max(tailMax,h);
                        minDuration = min(minDuration,duration), maxDuration=max(maxDuration,duration);
                    }
                } else glyph(position,"noteheads.s0"_, colors.value(noteIndex,black));
                positions << vec2(position); noteIndex++;
                if(duration==3 || duration==6 || duration==12 || duration == 24) glyph(position+int2(16,4),"dots.dot"_);
            }
            if(tailMin<=tailMax) {
                bool tailUp = !s;
                int x = page(s,t,0).x + (tailUp ? 12 : 0);
                line(vec2(x+0.5, page(s,t,tailMax).y+(tailUp?0:32)),vec2(x+0.5, page(s,t,tailMin).y+(tailUp?-32:0)),2);
                //assert(minDuration==maxDuration,minDuration,maxDuration);
                //if(minDuration!=maxDuration) Text(String("!"_)).render(int2(x,page(s,t,tailMin).y));
            }
        }

        t = i+1<notes.size() ? notes.keys[i+1]*4/ticksPerBeat : t+beatsPerMeasure;
        if(t/beatsPerMeasure>lastMeasure) { // Links quaver tails
            for(int s: range(2)) {
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
            }
        }
    }
}
