#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ffmpeg.h"
#include "encoder.h"
#include "window.h"
#include "audio.h"

struct Music {
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
    Window window {&sheet, encoder.size()/(preview?1:1), "MusicXML"_};
    AudioOutput audio {{this,&Music::read}};
    uint64 audioTime = 0, videoTime = 0;

    void expect() {
        while(!expected && chordIndex<sheet.notes.size()-1) {
            const array<Note>& chord = sheet.notes.values[chordIndex];
            for(uint i: range(chord.size)) {
                uint key = chord[i].key, index = chord[i].blitIndex;
                if(!expected.contains(key)) {
                    expected.insert(key, index);
                    //errors = 0; showExpected = false; // Hides highlighting while succeeding
                }
            }
            chordIndex++;
        }
    }

    Music() {
        expect();
        midi.noteEvent.connect([this](const uint key, const uint vel){
            if(vel) {
                if(expected.contains(key)) {
                    active.insertMulti(key,expected.at(key));
                    expected.remove(key);
                }
                //else if(!showExpected) { errors++; if(errors>1) showExpected = true; } // Shows expected notes on errors (allows one error before showing)
                else return; // No changes
            } else if(key) {
                if(active.contains(key)) while(active.contains(key)) active.remove(key);
                else return; // No changes
            }
            expect();
            sheet.colors.clear();
            for(uint index: active.values) sheet.colors.insert(index, red);
            if(active) target = min(apply(active.values,[this](uint index){return sheet.blits[index].position.x;}));
            contentChanged = true;
        });

        window.background = Window::White;
        if(preview) { window.show(); audio.start(mp3.rate, mp3.rate/encoder.fps); }
        else while(step()) {};
    }

    uint read(const mref<short2>& output) {
        assert(preview);
        assert(audio.rate == mp3.rate);
        size_t readSize = mp3.read(output);
        assert(readSize <= output.size);

        if(audioTime*encoder.fps > videoTime*mp3.rate) step(); // FIXME: Previews jitters with period size granularity

        audioTime += readSize;
        return readSize;
    }

    bool step() {
        if(midi.time > midi.duration) return false;
        midi.read(encoder.videoTime*encoder.rate/encoder.fps);
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
        }
        if(encode) encoder.writeVideoFrame(image);
        videoTime++;
        return true;
    }
} app;

