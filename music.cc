#include "music.h"
#include "process.h"
#include "file.h"
#include "interface.h"
#include "window.h"
#include "pdf.h"
#include <sys/resource.h>

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
#define ui(s) s
#else
#define ui(s)
#endif

    void start(array<string>&& arguments) {
        ui( window.keyPress.connect(this,&Music::keyPress); )
        for(auto&& path : arguments) {
            if(path.endsWith(".sfz"_) && exists(path)) {
                sampler.open(path);
                seq.noteEvent.connect(&sampler,&Sampler::event);
                audio.setInput(&sampler);
            } else if(path.endsWith(".mid"_)) {
                if(exists(path)) {
                    midi.open(path);
                    sampler.timeChanged.connect(&midi,&MidiFile::update);
                    midi.noteEvent.connect(&sampler,&Sampler::event);
                } else {
                    seq.recordMID(path);
                }
            }
#ifdef UI
            else if(path.endsWith(".pdf"_) && exists(path)) {
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
        setPriority(-20);
        if(audio.input) audio.start();
    }
    void keyPress(Key key) { if(key==Escape) running=false; }
} music;
