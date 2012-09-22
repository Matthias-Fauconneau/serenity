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

struct Music : Application, Widget {
    ICON(music) Window window __(this,int2(-1,-1),"Music"_,musicIcon());
    Sampler sampler;
    MidiFile midi;
    Scroll<PDF> sheet;
    Score score;

    Thread& thread = heap<Thread>(-20);
    //Thread& thread = defaultThread;
    AudioOutput audio __({&sampler, &Sampler::read},thread,true);
    Sequencer input __(thread);

    //~Music() { writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"conservative"_); }
    //Music(){ writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"performance"_);
    Music(){
        window.localShortcut(Key(' ')).connect(this,&Music::togglePlay);
        window.localShortcut(Escape).connect(this,&Application::quit);
        auto args = arguments();
        args<<"/Samples/Salamander.sfz"_;
        args<<"Documents/Sheets/Game of Thrones.mid"_;
        args<<"Documents/Sheets/Game of Thrones.pdf"_;
        for(ref<byte> path : args) {
            if(endsWith(path, ".sfz"_) && existsFile(path)) {
                window.setTitle(section(section(path,'/',-2,-1),'.',0,-2));
                sampler.open(path);
                sampler.progressChanged.connect(this,&Music::showProgress);
                input.noteEvent.connect(&sampler,&Sampler::noteEvent);
            }
            else if(endsWith(path, ".mid"_)) {
                if(existsFile(path,home())) {
                    midi.open(readFile(path,home()));
                    midi.noteEvent.connect(&sampler,&Sampler::noteEvent);
                } else {
                    input.recordMID(path);
                }
            }
            else if(endsWith(path, ".pdf"_) && existsFile(path,home())) {
                window.setTitle(section(section(path,'/',-2,-1),'.',0,-2));

                sheet.contentChanged.connect(&window,&Window::render);
                sheet.onGlyph.connect(&score,&Score::onGlyph);
                sheet.onPath.connect(&score,&Score::onPath);
                sheet.open(path,home());

                score.synchronize(move(midi.notes));
                debug(sheet.setAnnotations(score.debug);)
                score.activeNotesChanged.connect(&sheet,&PDF::setColors);
                score.nextStaff.connect(this,&Music::nextStaff);
                score.seek(0);
                midi.noteEvent.connect(&score,&Score::noteEvent);
                input.noteEvent.connect(&score,&Score::noteEvent);
            } else if(endsWith(path,".wav"_) && !existsFile(path,home())) {
                sampler.recordWAV(path);
            } else error("Unsupported"_,path);
        }
        window.show();
    }
    int current=0,count=0;
    void showProgress(int current, int count) {
        this->current=current; this->count=count;
        if(current==count) {
            window.backgroundCenter=window.backgroundColor=0xFF;
            window.widget=&sheet.area();
            window.setSize(int2(-1,-1));
            audio.start();
        }
        window.render();
    }
    void render(int2 position, int2 size) {
        if(current!=count) {
            Progress(0,count,current).render(position,size);
            if(sampler.lock && sampler.lock<sampler.full) Text(string(dec(100*sampler.lock/sampler.full)+"%"_)).render(position,size);
        }
    }
    bool play=false;
    void togglePlay() {
        play=!play;
        if(play) { midi.seek(0); sampler.timeChanged.connect(&midi,&MidiFile::update); }
        else sampler.timeChanged.delegates.clear();
    }
    void nextStaff(float top,float bottom) {
        float scale = sheet.size.x/(sheet.x2-sheet.x1)/sheet.normalizedScale;
        sheet.center(int2(sheet.size.x/2,scale*(top+bottom)/2));
    }
};
Application(Music)
