#include "process.h"
#include "file.h"

#include "sequencer.h"
#include "sampler.h"
#include "asound.h"
#include "midi.h"

#ifdef PDF
#include "pdf.h"
#endif

#include "window.h"
#include "interface.h"

struct Music : Application, Widget {
    ICON(music) Window window __(this,0,"Music"_,musicIcon());
    Sampler sampler;
    //AudioOutput audio __( __(&sampler, &Sampler::read) );
    Sequencer seq;

#if MIDI
    MidiFile midi;
#endif
#ifdef PDF
    PDF sheet;
    //Score score;
    map<int, int> notes; //[midiIndex] = note, indexOf(midiIndex) = scoreIndex
#endif
    ~Music() { writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"conservative"_); }
    Music() {
        writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"performance"_);
        window.localShortcut(Escape).connect(this,&Application::quit);
        auto args = arguments(); //if(!args) args<<"/Samples/Salamander.sfz"_;
        for(ref<byte> path : args) {
            if(endsWith(path, ".sfz"_) && existsFile(path)) {
                window.setTitle(section(section(path,'/',-2,-1),'.',0,-2));
                sampler.open(path);
                sampler.progressChanged.connect(this,&Music::showProgress);
                seq.noteEvent.connect(&sampler,&Sampler::queueEvent);
            }
#if MIDI
            else if(endsWith(path, ".mid"_)) {
                if(existsFile(path)) {
                    midi.open(path);
                    sampler.timeChanged.connect(&midi,&MidiFile::update);
                    midi.noteEvent.connect(&sampler,&Sampler::event);
                } else {
                    seq.recordMID(path);
                }
            }
#endif
#if PDF
            else if(endsWith(path, ".pdf"_) && existsFile(path)) {
                //sheet.onGlyph.connect(&score,&Score::onGlyph);
                //sheet.onPath.connect(&score,&Score::onPath);
                sheet.open(path);
                window.rename(section(section(path,'/',-2,-1),'.',0,-2));
                window.render();
                window.show();
                //score->synchronize(midi->notes);
                /*map<int, map<int, int> > sort; //[chronologic][bass to treble order] = index
                for(int i=0;i<events.count();i++) {
                    MidiEvent e = events[i];
                    if(e.type==NoteOn) sort[e.tick].insertMulti(e.note,i);
                }
                foreach(int tick,sort.keys()) foreach(int note,sort[tick].keys()) {
                    toScore[sort[tick][note]]=notes.count(); notes << events[sort[tick][note]].note;
                }*/
            } /*else if(path.endsWith(".wav"_)) && !existsFile(path) && sampler) {
                sampler->recordWAV(File(path,root(),Write));
            }*/
#endif
            else error("Unsupported"_,path);
        }
        //assert(sampler);
        sampler.lock();
        //audio.start(true);
        //sampler.queueEvent(64,127);
        window.show();
    }
    int current=0,count=0;
    void render(int2 position, int2 size){ if(current!=count) Progress(0,count,current).render(position,size); }
    void showProgress(int current, int count) {
        log(current,count);
        this->current=current; this->count=count; window.render(); //display loading progress
        if(current==count) { //loading completed
            //window.bgCenter=window.bgOuter=0; //set black background
            //window.bgCenter=window.bgOuter=0xFF; //set white background
            setPriority(-20); //raise priority
        }
    }
};
Application(Music)
