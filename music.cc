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
    typedef array<int> Chord;
    map<int,Chord> notes;
    void parse(map<int,Chord>&& notes) {
        this->notes=move(notes);
    }
    enum Clef { Treble, Bass };

    // Returns staff coordinates from note  (for a given clef and key)
    int staffY(Clef clef, int note) {
        int key[11] = {0,1,0,1,1,0,1,0,1,0,1}; // C major
        int h=note/12*7; for(int i=0;i<note%12;i++) h+=key[i];
        const int trebleOffset = 6*7+3; // C0 position in intervals from top line
        const int bassOffset = 4*7+5; // C0 position in intervals from top line
        int clefOffset = clef==Treble ? trebleOffset : clef==Bass ? bassOffset : 0;
        return clefOffset-h;
    }

    int2 position=0, size=0;
    // Returns staff X position from time
    int staffX(int t) {
        const int systemHeader = 128;
        const int timeInterval = 16, staffTime = (size.x-systemHeader)/timeInterval;
        return systemHeader+t%staffTime*timeInterval;
    }
    // Returns system line index from time
    int systemLine(int t) {
        const int systemHeader = 128;
        const int timeInterval = 16, staffTime = (size.x-systemHeader)/timeInterval;
        return t/staffTime;
    }

    // Returns page coordinates from staff coordinates
    int2 page(int staff, int t, int h) {
        const int staffCount = 2;
        const int staffInterval = 16, staffMargin = 2*staffInterval, staffHeight = staffMargin+4*staffInterval+staffMargin, systemHeight=staffCount*staffHeight+2*staffMargin;
        return position+int2(staffX(t),systemLine(t)*systemHeight+staff*staffHeight+2*staffMargin+h*staffInterval/2);
    }

    // Draws a staff
    void staff(int t, int staff, Clef unused clef) {
        //TODO: draw clef
        for(int i: range(5)) {
            int y = page(staff, t, i*2).y;
            line(int2(position.x, y), int2(position.x+size.x, y));
        }
    }
    // Draws a ledger line
    void ledger(int staff, int t, int h) {
        int2 p = page(staff, t, h);
        line(p+int2(-16,0),p+int2(16,0));
    }

    // Draws a note
    int lastSystem=-1;
    void note(int t, int s, int note) {
        Clef staffs[2] = {Treble, Bass};
        if(systemLine(t)>lastSystem) {
            staff(t, 0, staffs[0]);
            staff(t, 1, staffs[1]);
        }
        Clef clef = staffs[s];
        int h = staffY(clef, note);
        for(int i=-2;i>=h;i-=2) ledger(s, t, i);
        for(int i=10;i<h;i+=2) ledger(s, t, i);
        int2 position = page(s, t, h);
        fill(position+Rect(int2(0,-8),int2(16,8)));
    }

    void render(int2 position, int2 size) {
        this->position=position, this->size=size;
        //TODO: Font("/usr/share/lilypond/2.16.0/fonts/otf/emmentaler-20.otf"_)
        //Text(str(notes)).render(position,size);
        for(pair<int,Chord> chords: notes) {
            int t = chords.key;
            const Chord& chord = chords.value;
            for(int tone: chord) {
                int staff = tone>=60 ? 0 : 1;
                //if((staff==0 && chord.contains(tone-12)) || (staff==1 && chord.contains(tone+12))) staff=!staff;
                note(t, staff, tone);
            }
        }
    }
};

/// SFZ sampler and PDF renderer (tested with Salamander)
struct Music : Widget {
    ICON(music) Window window __(this,int2(0,0),"Piano"_,musicIcon());
    Sampler sampler;
    MidiFile midi;
    Scroll<PDF> pdfScore;
    Scroll<MidiScore> midiScore;
    Score score;
    List<Text> sheets;
    Folder folder __("Sheets"_);

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

        pdfScore.contentChanged.connect(&window,&Window::render);
        pdfScore.onGlyph.connect(&score,&Score::onGlyph);
        pdfScore.onPath.connect(&score,&Score::onPath);

        score.activeNotesChanged.connect(&pdfScore,&PDF::setColors);
        score.nextStaff.connect(this,&Music::nextStaff);
        midi.noteEvent.connect(&score,&Score::noteEvent);
        input.noteEvent.connect(&score,&Score::noteEvent);

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
        float scale = pdfScore.size.x/(pdfScore.x2-pdfScore.x1)/pdfScore.normalizedScale;
        pdfScore.delta.y = -min(scale*current, max(scale*previous, scale*next-pdfScore.size.y));
    }

    /// Toggles MIDI playing
    bool play=false;
    void togglePlay() {
        play=!play;
        if(play) { midi.seek(0); score.seek(0); sampler.timeChanged.connect(&midi,&MidiFile::update); }
        else sampler.timeChanged.delegates.clear();
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
            midiScore.parse(move(midi.notes));
            window.widget = &midiScore.area();
        }
        window.setSize(int2(0,0));
        window.render();
    }
} application;
