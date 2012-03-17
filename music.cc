#include "process.h"
#include "file.h"

#include "sequencer.h"
#include "sampler.h"
#include "alsa.h"
#include "midi.h"

#ifdef UI
#include "pdf.h"
#include "interface.h"
#include "window.h"
#endif

#include <sys/resource.h>

#include "array.cc"

struct Music : Application {
    Sequencer seq;
    Sampler sampler;
    AudioOutput audio{true};
    MidiFile midi;

#ifdef UI
    PDF sheet;
    //Score score;
    map<int, int> notes; //[midiIndex] = note, indexOf(midiIndex) = scoreIndex
    Window window{sheet,int2(1280,1024),"Music"_};
#endif
    void start(array<string>&& arguments) {
#ifdef UI
        window.keyPress.connect(this,&Music::keyPress);
#endif
        for(auto&& path : arguments) {
            if(endsWith(path, ".sfz"_) && exists(path)) {
                sampler.open(path);
                seq.noteEvent.connect(&sampler,&Sampler::event);
                audio.read = {&sampler, &Sampler::read};
            } else if(endsWith(path, ".mid"_)) {
                if(exists(path)) {
                    midi.open(path);
                    sampler.timeChanged.connect(&midi,&MidiFile::update);
                    midi.noteEvent.connect(&sampler,&Sampler::event);
                } else {
                    seq.recordMID(path);
                }
            }
#ifdef UI
            else if(endsWith(path, ".pdf"_) && exists(path)) {
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
            } /*else if(path.endsWith(".wav"_)) && !exists(path) && sampler) {
                sampler->recordWAV(path);
            }*/
#endif
            else error("Unhandled argument"_,path);
        }
        if(!sampler) error("Usage: music instrument.sfz [music.mid] [sheet.pdf] [output.wav]"_);
        //sheet.scroll=1600;
#ifndef DEBUG
        setPriority(-20);
#endif
        audio.start();
    }
#ifdef UI
    void keyPress(Key key) { if(key==Escape) running=false; }
#endif
} music;
