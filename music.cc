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
#include "midiscore.h"

// Simple human readable/editable format for score synchronization annotations
map<int, Chord> parseAnnotations(string&& annotations) {
    map<int, Chord> chords;
    uint t=0,i=0;
    for(TextData s(annotations);s;) {
        while(!s.match('\n')) {
            int key = s.integer(); s.match(' ');
            chords[t] << MidiNote __(key,t,1);
            i++;
        }
        t++;
    }
    return chords;
}

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
        for(string& file : files) {
            if(endsWith(file,".mid"_)||endsWith(file,".pdf"_)) {
                for(const Text& text: sheets) if(text.text==section(file,'.')) goto break_;
                /*else*/ sheets << string(section(file,'.'));
                break_:;
            }
        }
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
        score.annotationsChanged.connect(this,&Music::annotationsChanged);
        midi.noteEvent.connect(&score,&Score::noteEvent);

        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(Key(' ')).connect(this,&Music::togglePlay);
        window.localShortcut(Key('o')).connect(this,&Music::showSheetList);
        window.localShortcut(Key('e')).connect(&score,&Score::toggleEdit);
        window.localShortcut(LeftArrow).connect(&score,&Score::previous);
        window.localShortcut(RightArrow).connect(&score,&Score::next);
        window.localShortcut(Insert).connect(&score,&Score::insert);
        window.localShortcut(Delete).connect(&score,&Score::remove);
        window.localShortcut(Return).connect(this,&Music::toggleAnnotations);
    }

    /// Shows samples loading progress. When loaded, displays any loaded sheet and starts audio output.
    int current=0,count=0;
    void showProgress(int current, int count) {
        if(current==count) {
            showSheetList();
            //openSheet("Adagio for TRON"_);
            //openSheet("Arrival at Aslans How"_);
            //openSheet("Avatar"_);
            //openSheet("Ballad of Serenity"_);
            //openSheet("Brave Adventurers"_);
            //openSheet("Enterprising Young Men"_);
            //openSheet("Father and Son"_);
            //openSheet("Forbidden Friendship (Easy)"_);
            //openSheet("Game of Thrones"_);
            //openSheet("Inception - Time"_);
            //openSheet("Kingdom Dance"_);
            //openSheet("Moonlight Sonata"_);
            //openSheet("Once Upon a Time in Africa"_);
            //openSheet("Romantic Flight (Easy)"_);
            //openSheet("Test Drive (Easy)"_);
            //openSheet("To Aslans Camp"_);
            //openSheet("Turret Opera (Cara Mia)"_);
            //openSheet("When Cultures Meet"_);
            audio.start();
        } else if(count!=this->count) window.setSize(int2(count,512));
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
        if(pdfScore.normalizedScale && (pdfScore.x2-pdfScore.x1)) {
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

    void toggleAnnotations() {
        if(pdfScore.annotations) pdfScore.annotations.clear(), window.render(); else pdfScore.setAnnotations(score.debug);
    }

    /// Opens the given PDF+MIDI sheet
    void openSheet(uint index) { openSheet(sheets[index].text); }
    string name;
    void openSheet(const ref<byte>& name) {
        if(play) togglePlay();
        score.clear(); midi.clear();
        this->name=string(name);
        window.setTitle(name);
        if(existsFile(string(name+".mid"_),folder)) midi.open(readFile(string(name+".mid"_),folder));
        window.backgroundCenter=window.backgroundColor=0xFF;
        if(existsFile(string(name+".pdf"_),folder)) {
            pdfScore.open(readFile(string(name+".pdf"_),folder));
            score.parse();
            if(midi.notes) score.synchronize(move(midi.notes));
            else if(existsFile(string(name+".not"_),folder)) score.annotate(parseAnnotations(readFile(string(name+".not"_),folder)));
            //pdfScore.setAnnotations(score.debug);
            window.widget = &pdfScore.area();
            pdfScore.delta = 0;
        } else {
            midiScore.parse(move(midi.notes),midi.key,midi.tempo,midi.timeSignature);
            window.widget = &midiScore.area();
            midiScore.delta=0;
            midiScore.widget().render(int2(0,0),int2(1280,0)); //compute note positions for score scrolling
            score.chords = copy(midiScore.notes);
            score.staffs = move(midiScore.staffs);
            score.positions = move(midiScore.positions);
        }
        score.seek(0);
        window.setSize(int2(0,0));
        window.render();
    }

    void annotationsChanged(const map<int, Chord>& chords) {
        pdfScore.setAnnotations(score.debug);
        string annotations;
        for(const_pair<int, Chord> chord: chords) {
            for(MidiNote note: chord.value) annotations <<dec(note.key)<<' ';
            annotations <<'\n';
        }
        writeFile(string(name+".not"_),annotations,folder);
    }
} application;
