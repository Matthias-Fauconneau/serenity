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

/// SFZ sampler and PDF renderer (tested with Salamander)
struct Music : Widget {
    ICON(music) Window window __(this,int2(0,0),"Piano"_,musicIcon());
    Sampler sampler;
    MidiFile midi;
    Scroll<PDF> sheet;
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
        for(string& file : files) if(endsWith(file,".mid"_) /*&& files.contains(section(file,'.')+".pdf"_)*/) sheets << string(section(file,'.'));
        sheets.itemPressed.connect(this,&Music::openSheet);

        sampler.open("/Samples/Salamander.sfz"_);
        sampler.progressChanged.connect(this,&Music::showProgress);
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        midi.noteEvent.connect(&sampler,&Sampler::noteEvent);

        sheet.contentChanged.connect(&window,&Window::render);
        sheet.onGlyph.connect(&score,&Score::onGlyph);
        sheet.onPath.connect(&score,&Score::onPath);

        score.activeNotesChanged.connect(&sheet,&PDF::setColors);
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
            openSheet("Adagio for TRON"_);
            //openSheet("Game of Thrones"_);
            //openSheet("Where's Hiccup"_);
            //openSheet("Romantic Flight (Easy)"_);
            //openSheet("Test Drive (Easy)"_);
            //openSheet("Forbidden Friendship (Easy)"_);
            //openSheet("Kingdom Dance"_);
            //openSheet("Turret Opera (Cara Mia)"_);
            //openSheet("Brave Adventurers"_);
            //openSheet("Father and Son"_);
            //openSheet("Inception - Time"_);
            //openSheet("Moonlight Sonata"_);
            //openSheet("Avatar"_);
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
    void nextStaff(float unused previous, float current, float unused next) {
        float scale = sheet.size.x/(sheet.x2-sheet.x1)/sheet.normalizedScale;
        //sheet.delta.y = -min(scale*current, max(scale*previous, scale*next-sheet.size.y));
        sheet.delta.y = -scale*current;
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
        sheet.delta=0;
        if(existsFile(string(name+".pdf"_),folder)) {
            sheet.open(string(name+".pdf"_),folder);
            score.synchronize(move(midi.notes));
            sheet.setAnnotations(score.debug);
            window.backgroundCenter=window.backgroundColor=0xFF;
            window.widget=&sheet.area();
            window.setSize(int2(-1,-1));
            window.render();
            score.seek(0);
        }
    }
} application;
