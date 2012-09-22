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
    Sampler sampler;
    Thread thread;
    AudioOutput audio __({&sampler, &Sampler::read},thread,true);
    Sequencer input __(thread);
    MidiFile midi;
    Scroll<PDF> sheet;
    Score score;
    ICON(music) Window window __(this,int2(-1,-1),"Music"_,musicIcon());

    ~Music() { writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"conservative"_); }
    Music(){
        writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"performance"_);
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
                    sampler.timeChanged.connect(&midi,&MidiFile::update);
                    midi.noteEvent.connect(&sampler,&Sampler::noteEvent);
                } else {
                    input.recordMID(path);
                }
            }
            else if(endsWith(path, ".pdf"_) && existsFile(path,home())) {
                sheet.onGlyph.connect(&score,&Score::onGlyph);
                sheet.onPath.connect(&score,&Score::onPath);
                sheet.open(path,home());
                window.setTitle(section(section(path,'/',-2,-1),'.',0,-2));
                score.synchronize(move(midi.notes));
                midi.noteEvent.connect(&score,&Score::noteEvent);
                score.highlight.connect(&sheet,&PDF::setHighlight);
                sheet.contentChanged.connect(&window,&Window::render);
                input.noteEvent.connect(&score,&Score::noteEvent);
                score.seek(0);
            } else if(endsWith(path,".wav"_) && !existsFile(path,home())) {
                sampler.recordWAV(path);
            } else error("Unsupported"_,path);
        }
        window.show();
        debug(showProgress(0,0);)
    }
    int current=0,count=0;
    void showProgress(int current, int count) {
        this->current=current; this->count=count;
        if(current==count) {
            window.backgroundCenter=window.backgroundColor=0xFF;
            window.widget=&sheet.area();
            window.setSize(int2(-1,-1));
            audio.start(); thread.spawn(-20);
        }
        window.render();
    }
    void render(int2 position, int2 size) {
        if(current!=count) {
            Progress(0,count,current).render(position,size);
            if(sampler.lock && sampler.lock<sampler.full) Text(string(dec(100*sampler.lock/sampler.full)+"%"_)).render(position,size);
        }
    }
};
Application(Music)
