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
#include "time.h"

struct Music : Application, Widget/*, Timer*/ {
    ICON(music) Window window __(this,int2(64,64),"Music"_,musicIcon());
    Sampler sampler;
    Thread thread;
    AudioOutput audio __({&sampler, &Sampler::read},thread,true);
    Sequencer input __(thread);

#if MIDI
    MidiFile midi;
#endif
#ifdef PDF
    PDF sheet;
    //Score score;
    map<int, int> notes; //[midiIndex] = note, indexOf(midiIndex) = scoreIndex
#endif
    ~Music() { writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"conservative"_); }
    Music(){
        writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"performance"_);
        //audio.underrun.connect(this,&Music::underrun);
        window.localShortcut(Escape).connect(this,&Application::quit);
        auto args = arguments(); if(!args) args<<"/Samples/Salamander.sfz"_;
        for(ref<byte> path : args) {
            if(endsWith(path, ".sfz"_) && existsFile(path)) {
                window.setTitle(section(section(path,'/',-2,-1),'.',0,-2));
                sampler.open(path);
                sampler.progressChanged.connect(this,&Music::showProgress);
                input.noteEvent.connect(&sampler,&Sampler::noteEvent);
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
        window.show();
        audio.start();
        thread.spawn(-20);
    }
    int current=0,count=0;
    void showProgress(int current, int count) {
        this->current=current; this->count=count;
        if(current==count) { window.backgroundCenter=window.backgroundColor=0; /*underrunCount=0;*/ }
        window.render();
    }
    //uint underrunCount=0; void underrun() { underrunCount++; setAbsolute(currentTime()+2); }
    //void event() { window.render(); }
    void render(int2 position, int2 size) {
        if(current!=count) {
            Progress(0,count,current).render(position,size);
            if(sampler.lock && sampler.lock<sampler.full) Text(string(dec(100*sampler.lock/sampler.full)+"%"_)).render(position,size);
        }
        //else { Text(string(dec(underrunCount))).render(position,size); }
    }
};
Application(Music)
