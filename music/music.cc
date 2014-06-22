#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ffmpeg.h"
#include "encoder.h"
#include "window.h"
#include "audio.h"

struct Music : Widget {
    string name = arguments()[0];
    MusicXML xml = readFile(name+".xml"_);
    Sheet sheet {xml.signs, xml.divisions};
    AudioFile mp3 = name+".mp3"_; // 48KHz AAC would be better
    MidiFile midi = readFile(name+".mid"_);
    buffer<uint> noteToBlit = sheet.synchronize(apply(filter(midi.notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;}));

    // Highlighting
    map<uint,uint> active; // Maps active keys to notes (indices)
    uint midiIndex = 0, noteIndex = 0;
    float target=0, speed=0, position=0; // X target/speed/position of sheet view

    // Encoding
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
        if(sheet.synchronizationFailed) { window.background=White; return; }
        window.background = NoBackground; // Only clear on changes
        window.widget = this;
        if(preview) audio.start(mp3.rate, 1024);
        //else while(midi.time < midi.duration) step();
    }

    uint read(const mref<short2>& output) {
        size_t readSize = mp3.read(output);
        audioTime += readSize;
        return readSize;
    }

    // Render loop
    void render(const Image& image) override {
        for(;midiIndex < midi.notes.size && midi.notes[midiIndex].time <= videoTime*midi.ticksPerSeconds/encoder.fps; midiIndex++) {
            uint key = midi.notes[midiIndex].key;
            uint vel = midi.notes[midiIndex].velocity;
            assert_(key);
            if(vel) { if(noteToBlit[noteIndex]!=uint(-1)) { active.insertMulti(key,noteToBlit[noteIndex]); contentChanged=true; } noteIndex++; }
            else if(!vel && active.contains(key)) { while(active.contains(key)) active.remove(key); contentChanged=true; }
            if(!contentChanged) continue;
            sheet.colors.clear();
            for(uint index: active.values) sheet.colors.insert(index, red);
            if(active) target = min(apply(active.values,[this](uint index){return sheet.blits[index].position.x;}));
        }

        // Smooth scroll animation (assumes constant time step)
        const float k=1./encoder.fps, b=1./encoder.fps; // Stiffness and damping constants
        //log(float(sheet.measures.last()*midi.ticksPerSeconds)/float(midi.duration*encoder.fps));
        speed = min(5.f, b*speed + k*(target-position)); // Euler integration of speed from forces of spring equation (capped to 5px/frame)
        position = position + speed; // Euler integration of position from speed
        if(sheet.position != round(position)) sheet.position = round(position), contentChanged = true;
        if(contentChanged) { renderBackground(image, White); sheet.render(image); contentChanged=false; }
        if(encode) encoder.writeVideoFrame(image);
        videoTime++;
        window.render();
    }
} app;

