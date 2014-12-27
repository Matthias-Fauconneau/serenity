/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "midi-input.h"
#include "sampler.h"
#include "asound.h"
#include "pdf-renderer.h"
#include "MusicXML.h"
#include "sheet.h"
#include "layout.h"
#include "interface.h"
#include "window.h"

struct GraphicsWidget : Graphics, Widget {
    GraphicsWidget(Graphics&& o) : Graphics(move(o)) {}
    vec2 sizeHint(vec2) override;
    shared<Graphics> graphics(vec2) override;
};

vec2 GraphicsWidget::sizeHint(vec2) { assert_(isNumber(bounds.max), bounds); return bounds.max; }
shared<Graphics> GraphicsWidget::graphics(vec2 unused size /*TODO: center*/) { return shared<Graphics>((Graphics*)this); }

/// SFZ sampler and PDF renderer
struct Music {
    Folder folder {"Scores"_, home()};
    array<String> files = folder.list(Files|Sorted);
    String title;

    const uint rate = 44100;
    Thread decodeThread;
    unique<Sampler> sampler = nullptr;
    Thread audioThread{-20};
    AudioOutput audio {audioThread};
    MidiInput input {audioThread};

    array<unique<FontData>> fonts;
    unique<Sheet> sheet = nullptr;
    unique<Scroll<HList<GraphicsWidget>>> pages;
    Window window {&pages->area(), 0};

    Music() {
        window.actions[DownArrow] = {this, &Music::nextTitle};
        window.actions[Return] = {this, &Music::nextTitle};
        window.actions[Key('1')] = [this]{ setInstrument("Maestro"); };
        window.actions[Key('2')] = [this]{ setInstrument("Blanchet"); };

        assert_(files);
        setTitle(arguments() ? arguments()[0] : files[0]);

        setInstrument("Maestro");

        AudioControl("Master Playback Switch") = 1;
        AudioControl("Headphone Playback Switch") = 1;
        AudioControl("Master Playback Volume") = 100;
        audio.start(sampler->rate, Sampler::periodSize, 32, 2);
        //assert_(audioThread);
    }
    ~Music() {
        decodeThread.wait(); // ~Thread
        audioThread.wait(); // ~Thread
    }

    void setInstrument(string name) {
        if(audioThread) audioThread.wait();
        if(decodeThread) decodeThread.wait(); // ~Thread
        input.noteEvent.delegates.clear();
        sampler = unique<Sampler>(rate, "/Samples/"+name+".sfz"_, decodeThread);
        sampler->pollEvents = {&input, &MidiInput::event}; // Ensures all events are received right before mixing
        input.noteEvent.connect(sampler.pointer, &Sampler::noteEvent);
        audio.read32 = {sampler.pointer, &Sampler::read32};
        audioThread.spawn();
        decodeThread.spawn();
    }
    void setTitle(string title) {
        if(endsWith(title,".pdf"_)||endsWith(title,".xml"_)) title=title.slice(0,title.size-4);
        this->title = copyRef(title);
        buffer<Graphics> pages;
        if(existsFile(title+".xml"_, folder)) {
            MusicXML musicXML (readFile(title+".xml"_, folder));
            sheet = unique<Sheet>(musicXML.signs, musicXML.divisions, window.size);
            pages = move(sheet->pages);
        } else {
            pages = decodePDF(readFile(title+".pdf"_, folder), fonts);
        }
        this->pages = unique<Scroll<HList<GraphicsWidget>>>( apply(pages, [](Graphics& o) { return GraphicsWidget(move(o)); }) );
        this->pages->horizontal = true;
        window.widget = window.focus = &this->pages->area();
        window.render();
        window.setTitle(title);
    }
    void nextTitle() {
        for(size_t index: range(files.size-1)) if(startsWith(files[index], title) && !startsWith(files[index+1], title)) { setTitle(section(files[index+1],'.', 0, -2)); break; }
    }
} app;
