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
    Window window {&sheet, encoder.size(), "MusicXML"_};
    Image image;
    bool contentChanged = true;
    Music() {
        midi.noteEvent.connect([this](const uint unused key, const uint unused vel){
            // TODO: highlight notes
            contentChanged = true;
        });

        window.background = Window::White;
        window.actions[Escape] = []{exit();};
        window.show();

        queue();
    }
    void event() {
        if(midi.time > midi.duration) return;
        midi.read(encoder.videoTime*encoder.rate/encoder.fps);
        int position = sheet.measures.last() * midi.time / midi.duration;
        if(sheet.position != position) sheet.position = position, contentChanged = true;
        if(contentChanged) {
            window.render(); window.event();
            image = share(window.target);
            assert_(image);
            //image = renderToImage(layout, encoder.size());
            contentChanged=false;
        }
        encoder.writeVideoFrame(image);
        queue();
    }
} app;

