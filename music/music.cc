#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "encoder.h"
#include "window.h"

struct Music : Poll {
    string name = arguments()[0];
    MusicXML xml = readFile(name+".xml"_);
    MidiFile midi = readFile(name+".mid"_);
    Sheet sheet {xml.signs, xml.divisions};
    buffer<uint> midiToBlit = sheet.synchronize(apply(midi.notes,[](MidiNote o){return o.key;}));
    Encoder encoder {name /*, {&audioFile, &AudioFile::read}*/}; //TODO: remux without reencoding
    bool preview = true;
    Time time;
    Window window {&sheet, encoder.size()/(preview?2:1), "MusicXML"_};
    Image image;
    bool contentChanged = true;

    map<uint,uint> active; // Maps active keys to notes (indices)
    map<uint,uint> expected; // Maps expected keys to notes (indices)
    uint chordIndex = 0;

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
            contentChanged = true;
        });

        window.background = Window::White;
        window.actions[Escape] = []{exit();};
        window.show();

        queue();
    }
    void event() {
        if(midi.time > midi.duration) return;
        midi.read(preview ? time*48 : encoder.videoTime*encoder.rate/encoder.fps);
        int position = int64(sheet.measures.last()-window.size.x) * int64(midi.time-midi.tracks[0].startTime) / int64(midi.duration);
        if(sheet.position != position) sheet.position = position, contentChanged = true;
        if(contentChanged) {
            window.render(); window.event();
            image = share(window.target);
            assert_(image);
            contentChanged=false;
        }
        if(!preview) encoder.writeVideoFrame(image);
        queue();
    }
} app;

