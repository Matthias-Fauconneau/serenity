#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ffmpeg.h"
#include "encoder.h"
#include "window.h"
#include "audio.h"

struct Music : Timer {
    string name = arguments()[0];
    MusicXML xml = readFile(name+".xml"_);
    Sheet sheet {xml.signs, xml.divisions};
    MidiFile midi = readFile(name+".mid"_);
    buffer<uint> midiToBlit = sheet.synchronize(apply(midi.notes,[](MidiNote o){return o.key;}));
    AudioFile mp3 = name+".mp3"_; // 48KHz AAC would be better

    // Highlighting
    map<uint,uint> active; // Maps active keys to notes (indices)
    map<uint,uint> expected; // Maps expected keys to notes (indices)
    uint chordIndex = 0;
    float target=0, speed=0, position=0; // X target/speed/position of sheet view

    // Encoding
    Image image;
    bool contentChanged = true;
    bool encode = false;
    Encoder encoder {name /*, {&audioFile, &AudioFile::read}*/}; //TODO: remux without reencoding

    // Preview
    bool preview = true;
    Timer timer;
    Window window {&sheet, encoder.size()/(preview?1:1), "MusicXML"_};
    AudioOutput audio {{this,&Music::read}};
    uint64 audioStartTime;
    uint64 audioTime = 0, videoTime = 0;

    Music() {
        log(midi.duration/48000., mp3.duration/48000.);
        log(midi.ticksPerBeat, uint64(mp3.duration) * uint64(midi.ticksPerBeat) / uint64(midi.duration));
        midi.ticksPerBeat = uint64(mp3.duration) * uint64(midi.ticksPerBeat) / uint64(midi.duration); //FIXME
        log(midi.ticksPerBeat);
        expect();
        midi.noteEvent.connect(this, &Music::noteEvent);
        window.background = Window::White;
        if(preview) { nextStepTimestamp=realTime(); step(); window.show(); audioStartTime = audio.start(mp3.rate, 8192); setAbsolute(audioStartTime+second/encoder.fps); }
        else while(step()) {};
    }

    void expect() {
        while(!expected && chordIndex<sheet.notes.size()-1) {
            const array<Note>& chord = sheet.notes.values[chordIndex];
            for(uint i: range(chord.size)) {
                uint key = chord[i].key, index = chord[i].blitIndex;
                if(!expected.contains(key)) expected.insert(key, index);
            }
            chordIndex++;
        }
    }

    void noteEvent(const uint key, const uint vel){
        assert_(key);
        if(vel && expected.contains(key)) { active.insertMulti(key,expected.at(key)); expected.remove(key); }
        else if(!vel && active.contains(key)) while(active.contains(key)) active.remove(key);
        else return; // No changes
        expect();
        sheet.colors.clear();
        for(uint index: active.values) sheet.colors.insert(index, red);
        if(active) target = min(apply(active.values,[this](uint index){return sheet.blits[index].position.x;}));
        contentChanged = true;
    }

    uint read(const mref<short2>& output) {
        size_t readSize = mp3.read(output);
        audioTime += readSize;
        return readSize;
    }

    int64 nextStepTimestamp = 0;
    void event() override {
        assert_(realTime() > nextStepTimestamp);
        step();
        while(window.lastCompletion < nextStepTimestamp) window.Poll::poll();
        int64 displayDelay = second; //window.lastCompletion-nextStepTimestamp;
        int64 videoTime = this->videoTime*second/encoder.fps;
        nextStepTimestamp = audioStartTime+videoTime-displayDelay;
        //int64 now = realTime();
        //assert_(nextStepTimestamp > now, int64(nextStepTimestamp-now)*1000/second, displayDelay*1000/second);
        setAbsolute(nextStepTimestamp);
    }

    bool step() {
        assert_(audioTime < videoTime*mp3.rate/encoder.fps);
        if(midi.time > midi.duration) return false;
        midi.read(videoTime*mp3.rate/encoder.fps);
        // Smooth scroll animation (assumes constant time step)
        const float k=1./60, b=1./60; // Stiffness and damping constants
        speed = b*speed + k*(target-position); // Euler integration of speed from forces of spring equation
        position = position + speed; // Euler integration of position from speed
        if(sheet.position != round(position)) sheet.position = round(position), contentChanged = true;
        if(contentChanged) {
            window.render(); window.event();
            image = share(window.target);
            assert_(image);
            contentChanged=false;
        } else window.lastCompletion=realTime();
        if(encode) encoder.writeVideoFrame(image);
        videoTime++;
        return true;
    }
} app;

