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
    enum Clef { Treble, Bass };
    Font font __("/usr/share/lilypond/2.16.0/fonts/otf/emmentaler-20.otf"_,128);
    map<int,Chord> notes;
    int key; uint tempo; uint timeSignature[2];

    const int staffCount = 2;
    const int staffInterval = 12, staffMargin = 4*staffInterval, staffHeight = staffMargin+4*staffInterval+staffMargin, systemHeight=staffCount*staffHeight+staffMargin;
    const int systemHeader = 128;
    int beatsPerMeasure;
    int staffTime;

    void parse(map<int,Chord>&& notes, int key, uint tempo, uint timeSignature[2]) {
        this->notes=move(notes);
        this->key=key;
        this->tempo=tempo;
        for(uint i: range(2)) this->timeSignature[i]=timeSignature[i];
        beatsPerMeasure = timeSignature[0]*timeSignature[1];
        staffTime = 5*beatsPerMeasure;
    }

    int2 sizeHint() { return int2(1280,systemHeight*(notes.keys.last()/staffTime)); }

    // Returns staff coordinates from note  (for a given clef and key)
    int staffY(Clef clef, int note) {
        int key[11] = {0,1,0,1,1,0,1,0,1,0,1}; // C major
        if(this->key==-1) key[9]=1, key[10]=0;
        else error("TODO: generic");
        int h=note/12*7; for(int i=0;i<note%12;i++) h+=key[i];
        const int trebleOffset = 6*7+3; // C0 position in intervals from top line
        const int bassOffset = 4*7+5; // C0 position in intervals from top line
        int clefOffset = clef==Treble ? trebleOffset : clef==Bass ? bassOffset : 0;
        return clefOffset-h;
    }

    int2 position=0, size=0;
    // Returns staff X position from time
    int staffX(int t) { return systemHeader+t%staffTime*(size.x-systemHeader-16)/staffTime; }

    // Returns page coordinates from staff coordinates
    int2 page(int staff, int t, int h) { return position+int2(staffX(t),t/staffTime*systemHeight+staff*staffHeight+2*staffMargin+h*staffInterval/2); }

    void glyph(int2 position, const ref<byte> name) {
        const Glyph& glyph = font.glyph(font.index(name));
        substract(position+glyph.offset,glyph.image);
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
        int lastSystem=-1, lastMeasure=0;
        this->position=position, this->size=size;
        //Text(str(notes)).render(position,size);
        array<MidiNote> active[2]; //0 = treble (right hand), 1 = bass (left hand)
        for(pair<int,Chord> chords: notes) {
            int t = chords.key;

            // Removes released notes from active sets
            for(uint s: range(2)) for(uint i=0;i<active[s].size();) if(active[s][i].start+active[s][i].duration<=t) active[s].removeAt(i); else i++;
            uint sustain[2] = { active[0].size(), active[1].size() }; // Remaining notes kept sustained

            if(t/staffTime>lastSystem) { // Draws system
                lastSystem=t/staffTime;
                drawStaff(t, 0, Treble);
                drawStaff(t, 1, Bass);
                lastMeasure=t/beatsPerMeasure;
                line(int2(position.x+16,page(0,t,0).y),int2(position.x+16,page(1,t,8).y));
                line(int2(position.x+size.x-16,page(0,t,0).y),int2(position.x+size.x-16,page(1,t,8).y));
            } else if(t/beatsPerMeasure>lastMeasure) { // Draws measure bars
                lastMeasure=t/beatsPerMeasure;
                line(page(0,t,0)-int2(8,0),page(1,t,8)-int2(8,0));
            }

            array<MidiNote> current[2]; // new notes to be pressed
            for(MidiNote note: chords.value) { //first rough split based on pitch
                int s = note.key<60; //middle C
                current[s] << note;
                active[s] << note;
            }
            for(int s: range(2)) { // then balances load on both hand
                while(
                      current[s] && // any notes to move ?
                      ((s==1 && current[s].last().key>52) || (s==0 && current[s].first().key<68)) && // prevent stealing from far notes (TODO: relative to last active)
                      active[s].size()>=active[!s].size() && // keep active notes balanced
                      current[s].size()>=current[!s].size() && // keep load balanced
                      (!current[!s] || abs(current[1].last().key-current[0].first().key)<=12) && // keep short span
                      (
                          active[s].size()>active[!s].size()+1 || // balance active notes
                          current[s].size()>current[!s].size()+1 || // balance load
                          // both new notes and active notes load are balanced
                          (current[0] && current[1] && s == 0 && abs(current[1].first().key-current[1].last().key)<abs(current[0].first().key-current[0].last().key)) || // minimize right span
                          (current[0] && current[1] && s == 1 && abs(current[0].first().key-current[0].last().key)<abs(current[1].first().key-current[1].last().key)) || // minimize left span
                          (sustain[s] && sustain[!s] && active[s][sustain[s]-1].start>active[!s][sustain[!s]-1].start) // load least recently used hand
                          )) {
                    if(s) {
                        current[!s].insertAt(0, current[s].pop());
                        active[!s].insertAt(0, active[s].pop());
                    } else {
                        current[!s] << current[s].take(0);
                        active[!s] << active[s].take(sustain[s]);
                    }
                }
                if(current[s].size()>=4 && current[!s].size()==1 && current[s].size()>current[!s].size()+1) {
                    log(s,current[0],"-",current[1]);
                    assert(current[s]);
                    assert((s==1 && current[s].last().key>52) || (s==0 && current[s].first().key<68));
                    assert(active[s].size()>=active[!s].size());
                    assert(current[s].size()>=current[!s].size());
                    assert(!current[!s] || abs(current[1].last().key-current[0].first().key)<=12);
                    assert(
                        active[s].size()>active[!s].size()+1 || // balance active notes
                        current[s].size()>current[!s].size()+1 || // balance load
                        // both new notes and active notes load are balanced
                        (sustain[s] && sustain[!s] && active[s][sustain[s]-1].start>active[!s][sustain[!s]-1].start) // load least recently used hand
                            );
                }
            }

            for(int s: range(2)) { // finally displays notes on the right staff
                Clef clef = s?Bass:Treble;
                bool tailUp=true; uint tailMin=-1, tailMax=0; uint minDuration=-1,maxDuration=0;
                for(MidiNote note: current[s]) { // draws notes
                    uint key=note.key, duration=note.duration;
                    int h = staffY(clef, key);
                    int2 position = page(s, t, h);

                    for(int i=-2;i>=h;i-=2) drawLedger(s, t, i);
                    for(int i=10;i<=h;i+=2) drawLedger(s, t, i);
                    if(duration <= 12) {
                        glyph(position, duration<=6?"noteheads.s2"_:"noteheads.s1"_);
                        if(tailUp) tailMin=min<uint>(tailMin,position.y-32), tailMax=max<uint>(tailMax,position.y);
                        else tailMin=min<uint>(tailMin,position.y), tailMax=max<uint>(tailMax,position.y+32);
                        minDuration = min(minDuration,duration), maxDuration=max(maxDuration,duration);
                    } else glyph(position,"noteheads.s0"_);
                    if(duration==3 || duration==6 || duration==12 || duration == 24) glyph(position+int2(16,4),"dots.dot"_);
                }
                if(tailMin<tailMax) {
                    int x = page(s,t,0).x+13;
                    line(int2(x,tailMin),int2(x,tailMax),1.5);
                    //assert(minDuration==maxDuration,minDuration,maxDuration);
                    if(minDuration!=maxDuration) Text(string("!"_)).render(int2(x,tailMin));
                    if(minDuration==1) glyph(int2(x+1,tailUp?tailMin:tailMax),tailUp?"flags.u4"_:"flags.d4"_);
                    else if(minDuration==2) glyph(int2(x+1,tailUp?tailMin:tailMax),tailUp?"flags.u3"_:"flags.d3"_);
                }
            }
        }
    }
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

    /*Sampler sampler;
    Thread thread __(-20);
    AudioOutput audio __({&sampler, &Sampler::read},thread,true);
    Sequencer input __(thread);*/

    ~Music() { writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"conservative"_); }
    Music() {
        writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"performance"_);

        array<string> files = folder.list(Files);
        for(string& file : files) if(endsWith(file,".mid"_)) sheets << string(section(file,'.'));
        sheets.itemPressed.connect(this,&Music::openSheet);

        /*sampler.open("/Samples/Salamander.sfz"_);
        sampler.progressChanged.connect(this,&Music::showProgress);
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        midi.noteEvent.connect(&sampler,&Sampler::noteEvent);
        input.noteEvent.connect(&score,&Score::noteEvent);*/
        openSheet("When Cultures Meet"_);

        pdfScore.contentChanged.connect(&window,&Window::render);
        pdfScore.onGlyph.connect(&score,&Score::onGlyph);
        pdfScore.onPath.connect(&score,&Score::onPath);

        score.activeNotesChanged.connect(&pdfScore,&PDF::setColors);
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
            //openSheet("Turret Opera (Cara Mia)"_);
            openSheet("When Cultures Meet"_);
            //audio.start();
        } else if(count!=this->count) window.setSize(int2(count,256));
        this->current=current, this->count=count;
        window.render();
    }
    void render(int2 position, int2 size) {
        if(current!=count) {
            Progress(0,count,current).render(position,size);
            //if(sampler.lock && sampler.lock<sampler.full) Text(string(dec(100*sampler.lock/sampler.full)+"%"_)).render(position,size);
        }
    }

    /// Called by score to scroll PDF as needed when playing
    void nextStaff(float previous, float current, float next) {
        float scale = pdfScore.size.x/(pdfScore.x2-pdfScore.x1)/pdfScore.normalizedScale;
        pdfScore.delta.y = -min(scale*current, max(scale*previous, scale*next-pdfScore.size.y));
    }

    /// Toggles MIDI playing
    bool play=false;
    void togglePlay() {
        play=!play;
        //if(play) { midi.seek(0); score.seek(0); sampler.timeChanged.connect(&midi,&MidiFile::update); } else sampler.timeChanged.delegates.clear();
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
            score.seek(0);
            window.widget = &pdfScore.area();
        } else {
            midiScore.parse(move(midi.notes),midi.key,midi.tempo,midi.timeSignature);
            window.widget = &midiScore.area();
        }
        window.setSize(int2(0,0));
        window.render();
    }
} application;
