#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ffmpeg.h"
#include "encoder.h"
#include "window.h"
#include "audio.h"

struct Music : Widget {
    Thread audioThread;
    string name = arguments()[0];
    MusicXML xml = readFile(name+".xml"_);
    Sheet sheet {xml.signs, xml.divisions, 720};
    AudioFile mp3 = name+".mp3"_; // 48KHz AAC would be better
    MidiFile midi = readFile(name+".mid"_);
    buffer<uint> noteToBlit = sheet.synchronize(apply(filter(midi.notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;}));

    // Highlighting
    map<uint,uint> active; // Maps active keys to notes (indices)
    uint midiIndex = 0, noteIndex = 0;
    float targetPosition=0, speed=0, position=0; // X target/speed/position of sheet view

    // Encoding
    bool encode = false;
    Encoder encoder {name /*, {&audioFile, &AudioFile::read}*/}; //TODO: remux without reencoding

    // Preview
    bool preview = !encode;
    Timer timer;
    Window window {this, int2(1280,720), "MusicXML"_};
    AudioOutput audio {{this,&Music::read}, audioThread};
    uint64 audioTime = 0, videoTime = 0;
    uint lastPosition = 0;

    uint noteIndexToMidiIndex(uint seekNoteIndex) {
        uint midiIndex=0;
        for(uint noteIndex=0; noteIndex<seekNoteIndex; midiIndex++) if(midi.notes[midiIndex].velocity) noteIndex++;
        return midiIndex;
    }

    Music() {
        window.background = White;
        //seek( midi.notes[noteIndexToMidiIndex(sheet.chordToNote[sheet.measureToChord[100+1]])].time );
        if(preview) { audio.start(mp3.rate, 1024); audioThread.spawn(); }
        else {
            Image target(window.size);
            while(mp3.position < mp3.duration) {
                step();
                renderBackground(target, White);
                position = min(float(sheet.measures.last()-target.size().x), position);
                sheet.render(target, int2(floor(-position), 0), target.size());
                encoder.writeVideoFrame(target);
            }
        }
    }

    uint read(const mref<short2>& output) {
        size_t readSize = mp3.read(output);
        audioTime += readSize;
        return readSize;
    }

    void seek(uint64 midiTime) {
        audioTime = midiTime * mp3.rate / midi.ticksPerSeconds;
        mp3.seek(audioTime); //FIXME: return actual frame time
        videoTime = audioTime*encoder.fps/mp3.rate; // FIXME: remainder
        //log(videoTime/float(encoder.fps), audioTime/float(mp3.rate), midiTime/float(midi.ticksPerSeconds));
        follow();
        position = targetPosition;
    }

    void follow() {
        Rect update = Rect(0);
        for(;midiIndex < midi.notes.size && midi.notes[midiIndex].time*encoder.fps <= videoTime*midi.ticksPerSeconds; midiIndex++) {
            MidiNote note = midi.notes[midiIndex];
            if(note.velocity) {
                if(noteToBlit[noteIndex]!=uint(-1)) {
                    uint blitIndex = noteToBlit[noteIndex];
                    active.insertMulti(note.key, blitIndex);
                    Sheet::Blit& blit = sheet.blits[blitIndex];
                    update |= blit.position+Rect(blit.image.size());
                }
                noteIndex++;
            }
            else if(!note.velocity && active.contains(note.key)) {
                while(active.contains(note.key)) {
                    uint blitIndex = active.take(note.key);
                    Sheet::Blit& blit = sheet.blits[blitIndex];
                    update |= blit.position+Rect(blit.image.size());
                }
            }
        }
        if(!update) return;

        sheet.colors.clear();
        for(uint index: active.values) sheet.colors.insert(index, red);
        if(active) targetPosition = min(apply(active.values,[this](uint index){return sheet.blits[index].position.x;}));
    }

    // Render loop
    void render(const Image& target) override {
        uint nextTime = window.msc-window.firstMSC;
        if(videoTime+1 != nextTime) { static int dropCount=0; int drop=nextTime-(videoTime+1); if(drop>0) { dropCount+=drop; log("Dropped",drop,dropCount); } }
        while(videoTime < nextTime) step();
        position = min(float(sheet.measures.last()-target.size().x), position);
        sheet.render(target, int2(floor(-position), 0), target.size());
        window.render();
    }
    void step() {
        follow();
        const float dt=1./encoder.fps, k=dt, b=-1; // Time step, stiffness (1/frame) and damping (1) constants
        speed += dt * (b*speed + k*(targetPosition-position)); // Euler integration of speed (px/frame) from forces of spring equation
        position += speed; // Euler integration of position (in pixels) from speed (in pixels per frame)
        videoTime++;
    }
} app;
