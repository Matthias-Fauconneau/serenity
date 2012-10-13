/// \file music.cc Keyboard (piano) practice application
#include "process.h"
#include "file.h"

#include "sequencer.h"
#include "sampler.h"
#include "asound.h"
#include "midi.h"

#include "window.h"
#include "interface.h"
#include "pdf.h"
#include "score.h"

struct MidiScore : Widget {
    enum Clef { Bass, Treble };
    int keys[2][11] = {
        {1,0,1,0,1,1,0,1,0,1,0}, // F minor
        {0,1,0,1,1,0,1,0,1,0,1}  // C major
    };
    int accidentals[2][12] = { //1=b, 2=#
        {0,1,0,1,0,0,1,0,1,0,0,0}, // F minor
        {0,2,0,2,0,0,2,0,2,0,2,0}  // C major
    };
    Font font __("/usr/share/lilypond/2.16.0/fonts/otf/emmentaler-20.otf"_,128);
    map<int,Chord> notes;
    int key=-1; uint tempo=120; uint timeSignature[2] = {4,4};

    const int staffCount = 2;
    const int staffInterval = 12, staffMargin = 4*staffInterval, staffHeight = staffMargin+4*staffInterval+staffMargin, systemHeight=staffCount*staffHeight+staffMargin;
    const int systemHeader = 128;
    int beatsPerMeasure;
    int staffTime;

    array<float> staffs;
    array<vec2> positions;

    void parse(map<int,Chord>&& notes, int unused key, uint tempo, uint timeSignature[2]) {
        this->notes=move(notes);
        this->key=key;
        this->tempo=tempo;
        if(this->timeSignature[0]==3 && this->timeSignature[0]==4) {
            this->timeSignature[0]=timeSignature[0];
            this->timeSignature[1]=timeSignature[1];
        }
        beatsPerMeasure = this->timeSignature[0]*this->timeSignature[1];
        staffTime = 5*beatsPerMeasure;
    }

    int2 sizeHint() { return int2(1280,systemHeight*(notes.keys.last()/staffTime+1)); }

    // Returns staff coordinates from note  (for a given clef and key)
    int staffY(Clef clef, int note) {
        assert(key>=-1 && key<=0);
        int h=note/12*7; for(int i=0;i<note%12;i++) h+=keys[key+1][i];
        const int trebleOffset = 6*7+3; // C0 position in intervals from top line
        const int bassOffset = 4*7+5; // C0 position in intervals from top line
        int clefOffset = clef==Treble ? trebleOffset : clef==Bass ? bassOffset : 0;
        return clefOffset-h;
    }

    int2 position=0, size=0;
    // Returns staff X position from time
    int staffX(int t) { return systemHeader+t%staffTime*(size.x-systemHeader-16)/staffTime; }

    // Returns page coordinates from staff coordinates
    int2 page(int staff, int t, int h) { return position+int2(staffX(t),t/staffTime*systemHeight+(!staff)*staffHeight+2*staffMargin+h*staffInterval/2); }

    void glyph(int2 position, const ref<byte> name, byte4 color=black) {
        const Glyph& glyph = font.glyph(font.index(name));
        substract(position+glyph.offset,glyph.image,color);
    }

    // Draws a staff
    void drawStaff(int t, int staff, Clef clef) {
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
        constexpr ref<byte> numbers[10] = {"zero"_,"one"_,"two"_,"three"_,"four"_,"five"_,"six"_,"seven"_,"eight"_,"nine"_};
        glyph(int2(position.x+16+64,page(staff, t, 2*2).y-1),numbers[timeSignature[0]]);
        glyph(int2(position.x+16+64,page(staff, t, 4*2).y-1),numbers[timeSignature[1]]);
    }
    // Draws a ledger line
    void drawLedger(int staff, int t, int h) {
        int2 p = page(staff, t, h);
        line(p+int2(-4,0),p+int2(16,0));
    }

    void render(int2 position, int2 size) {
        staffs.clear(); positions.clear();
        int lastSystem=-1; uint lastMeasure=0, noteIndex=0;
        this->position=position, this->size=size;
        //Text(str(notes)).render(position,size);
        array<MidiNote> active[2]; //0 = treble (right hand), 1 = bass (left hand)
        array<MidiNote> quavers[2]; // for quaver linking
        for(pair<int,Chord> chords: notes) {
            uint t = chords.key;

            // Removes released notes from active sets
            for(uint s: range(2)) for(uint i=0;i<active[s].size();) if(active[s][i].start+active[s][i].duration<=t) active[s].removeAt(i); else i++;
            uint sustain[2] = { active[0].size(), active[1].size() }; // Remaining notes kept sustained

            if(int(t/staffTime)>lastSystem) { // Draws system
                lastSystem=t/staffTime;
                drawStaff(t, 0, Bass);
                drawStaff(t, 1, Treble);
                line(int2(position.x+16,page(1,t,0).y),int2(position.x+16,page(0,t,8).y));
                line(int2(position.x+size.x-16,page(1,t,0).y),int2(position.x+size.x-16,page(0,t,8).y));
                staffs << staffMargin+(lastSystem+1)*systemHeight;
            } else if(t/beatsPerMeasure>lastMeasure) { // Draws measure bars
                line(page(1,t,0)-int2(8,0),page(0,t,8)-int2(8,0));
            }

            if(t/beatsPerMeasure>lastMeasure) { // Links quaver tails
                for(int s: range(2)) {
                    Clef clef = (Clef)s;
                    bool tailUp=true; int dx = tailUp ? 12 : 0; uint slurY=tailUp?-1:0;
                    uint begin=0;
                    for(uint i: range(quavers[s].size())) {
                        MidiNote note = quavers[s][i];
                        int2 position = page(s, note.start, staffY(clef, note.key));
                        if(tailUp) slurY=min<uint>(slurY,position.y);
                        else slurY=max<uint>(slurY,position.y);
                        uint duration=note.duration;
                        if(i+1>=quavers[s].size() || quavers[s][i+1].duration<duration || (quavers[s][i+1].start != note.start && quavers[s][i+1].start != note.start+duration)) {
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
                lastMeasure=t/beatsPerMeasure;
            }

            array<MidiNote> current[2]; // new notes to be pressed
            for(MidiNote note: chords.value) { //first rough split based on pitch
                int s = note.key>=60; //middle C
                current[s] << note;
                active[s] << note;
            }
            for(int s: range(2)) { // then balances load on both hand
                while(
                      current[s] && // any notes to move ?
                      ((s==0 && current[s].last().key>=52) || (s==1 && current[s].first().key<68)) && // prevent stealing from far notes (TODO: relative to last active)
                      current[s].size()>=current[!s].size() && // keep new notes balanced
                      active[s].size()>=active[!s].size() && // keep active (sustain+new) notes balanced
                      (!current[!s] ||
                       (s==1 && abs(current[!s].first().key-current[s].first().key)<=12) || // keep short span on new notes (left)
                       (s==0 && abs(current[!s].last().key-current[s].last().key)<=12) ) && // keep short span on new notes (right)
                      (!sustain[!s] ||
                       (s==1 && abs(active[!s][0].key-current[s].first().key)<=18) || // keep short span with active notes (left)
                       (s==0 && abs(active[!s][sustain[!s]-1].key-current[s].last().key)<=18) ) && // keep short span with active notes (right)
                      (
                          active[s].size()>active[!s].size()+1 || // balance active notes
                          current[s].size()>current[!s].size()+1 || // balance load
                          // both new notes and active notes load are balanced
                          (current[0] && current[1] && s == 0 && abs(current[1].first().key-current[1].last().key)<abs(current[0].first().key-current[0].last().key)) || // minimize left span
                          (current[0] && current[1] && s == 1 && abs(current[0].first().key-current[0].last().key)<abs(current[1].first().key-current[1].last().key)) || // minimize right span
                          (sustain[s] && sustain[!s] && active[s][sustain[s]-1].start>active[!s][sustain[!s]-1].start) // load least recently used hand
                          )) {
                    if(!s) {
                        current[!s].insertAt(0, current[s].pop());
                        active[!s].insertAt(0, active[s].pop());
                    } else {
                        current[!s] << current[s].take(0);
                        active[!s] << active[s].take(sustain[s]);
                    }
                }
            }

            for(int s: range(2)) { // finally displays notes on the right staff
                Clef clef = (Clef)s;
                int tailMin=100, tailMax=-100; uint minDuration=-1,maxDuration=0;
                for(MidiNote note: current[s]) { // draws notes
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
                    //if(minDuration!=maxDuration) Text(string("!"_)).render(int2(x,page(s,t,tailMin).y));
                }
            }
        }
    }
    map<int,byte4> colors;
    signal<> contentChanged;
    void setColors(const map<int,byte4>& colors) { this->colors=copy(colors); contentChanged(); }
};

/// SFZ sampler and PDF renderer (tested with Salamander)
struct Music : Widget {
    Folder folder __("Sheets"_);
    ICON(music) Window window __(this,int2(0,0),"Piano"_,musicIcon());
    List<Text> sheets;

    MidiFile midi;
    Scroll<PDF> pdfScore;
    Scroll<MidiScore> midiScore;
    Score score;

    Sampler sampler;
    Thread thread __(-20);
    AudioOutput audio __({&sampler, &Sampler::read},thread,true);
    Sequencer input __(thread);

    ~Music() { writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"conservative"_); }
    Music() {
        writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"performance"_);

        array<string> files = folder.list(Files);
        for(string& file : files) if(endsWith(file,".mid"_)) sheets << string(section(file,'.'));
        sheets.itemPressed.connect(this,&Music::openSheet);

        sampler.open("/Samples/Salamander.sfz"_);
        sampler.progressChanged.connect(this,&Music::showProgress);
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        midi.noteEvent.connect(&sampler,&Sampler::noteEvent);
        input.noteEvent.connect(&score,&Score::noteEvent);

        pdfScore.contentChanged.connect(&window,&Window::render);
        pdfScore.onGlyph.connect(&score,&Score::onGlyph);
        pdfScore.onPath.connect(&score,&Score::onPath);

        midiScore.contentChanged.connect(&window,&Window::render);

        score.activeNotesChanged.connect(&pdfScore,&PDF::setColors);
        score.activeNotesChanged.connect(&midiScore,&MidiScore::setColors);
        score.nextStaff.connect(this,&Music::nextStaff);
        midi.noteEvent.connect(&score,&Score::noteEvent);

        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(Key(' ')).connect(this,&Music::togglePlay);
        window.localShortcut(Key('o')).connect(this,&Music::showSheetList);
    }

    /// Shows samples loading progress. When loaded, displays any loaded sheet and starts audio output.
    int current=0,count=0;
    void showProgress(int current, int count) {
        if(current==count) {
            showSheetList();
            //openSheet("Adagio for TRON"_);
            //openSheet("Avatar"_);
            //openSheet("Arrival at Aslans How"_);
            //openSheet("Brave Adventurers"_);
            //openSheet("Father and Son"_);
            //openSheet("Forbidden Friendship (Easy)"_);
            //openSheet("Game of Thrones"_);
            //openSheet("Inception - Time"_);
            //openSheet("Kingdom Dance"_);
            //openSheet("Moonlight Sonata"_);
            //openSheet("Romantic Flight (Easy)"_);
            //openSheet("Test Drive (Easy)"_);
            openSheet("To Aslans Camp"_);
            //openSheet("Turret Opera (Cara Mia)"_);
            //openSheet("When Cultures Meet"_);
            audio.start();
        } else if(count!=this->count) window.setSize(int2(count,256));
        this->current=current, this->count=count;
        window.render();
    }
    void render(int2 position, int2 size) {
        if(current!=count) {
            Progress(0,count,current).render(position,size);
            if(sampler.lock && sampler.lock<sampler.full) Text(string(dec(100*sampler.lock/sampler.full)+"%"_)).render(position,size);
        }
    }

    /// Called by score to scroll PDF as needed when playing
    void nextStaff(float previous, float current, float next) {
        if(pdfScore) {
            float scale = pdfScore.size.x/(pdfScore.x2-pdfScore.x1)/pdfScore.normalizedScale;
            pdfScore.delta.y = -min(scale*current, max(scale*previous, scale*next-pdfScore.ScrollArea::size.y));
        }
        midiScore.delta.y = -min(current, max(previous, next-midiScore.ScrollArea::size.y));
    }

    /// Toggles MIDI playing
    bool play=false;
    void togglePlay() {
        play=!play;
        if(play) { midi.seek(0); score.seek(0); sampler.timeChanged.connect(&midi,&MidiFile::update); } else sampler.timeChanged.delegates.clear();
    }

    /// Shows PDF+MIDI sheets selection to open
    void showSheetList() {
        window.widget=&sheets;
        window.render();
    }

    /// Opens the given PDF+MIDI sheet
    void openSheet(uint index) { openSheet(sheets[index].text); }
    void openSheet(const ref<byte>& name) {
        if(play) togglePlay();
        score.clear();
        window.setTitle(name);
        midi.open(readFile(string(name+".mid"_),folder));
        pdfScore.delta = 0;
        window.backgroundCenter=window.backgroundColor=0xFF;
        if(existsFile(string(name+".pdf"_),folder)) {
            pdfScore.open(string(name+".pdf"_),folder);
            score.synchronize(move(midi.notes));
            //pdfScore.setAnnotations(score.debug);
            window.widget = &pdfScore.area();
        } else {
            midiScore.parse(move(midi.notes),midi.key,midi.tempo,midi.timeSignature);
            window.widget = &midiScore.area();
            midiScore.widget().render(int2(0,0),int2(1280,0));
            score.chords = copy(midiScore.notes);
            score.staffs = move(midiScore.staffs);
            score.positions = move(midiScore.positions);
        }
        score.seek(0);
        window.setSize(int2(0,0));
        window.render();
    }
} application;
