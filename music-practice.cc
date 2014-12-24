/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "midi-input.h"
#include "sampler.h"
#include "asound.h"
#include "pdf-renderer.h"
#include "layout.h"
#include "interface.h"
#include "window.h"

struct GraphicsWidget : Graphics, Widget {
    GraphicsWidget(Graphics&& o) : Graphics(move(o)) {}
    int2 sizeHint(int2) override;
    shared<Graphics> graphics(int2) override;
};

int2 GraphicsWidget::sizeHint(int2) { return int2(this->size); }
shared<Graphics> GraphicsWidget::graphics(int2 unused size /*TODO: center*/) { return shared<Graphics>((Graphics*)this); }

/// SFZ sampler and PDF renderer
struct Music {
    Folder folder {"Scores"_, home()};
    String title;

    const uint rate = 44100;
    Thread decodeThread;
    Sampler sampler {rate, "/Samples/Maestro.sfz"_, [](uint){}, decodeThread}; // Audio mixing (consumer thread) preempts decoder running in advance (in producer thread (main thread))
    Thread audioThread;
    AudioOutput audio {{&sampler, &Sampler::read32}, audioThread};
    MidiInput input;

    array<unique<Font>> fonts;
    unique<Scroll<HList<GraphicsWidget>>> pages;
    Window window {&pages->area(), int2(0, 768)};

    Music() {
        setTitle(arguments()[0]);
        window.actions[DownArrow] = {this, &Music::nextTitle};
        window.actions[Return] = {this, &Music::nextTitle};
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        decodeThread.spawn();
        AudioControl("Master Playback Switch") = 1;
        AudioControl("Headphone Playback Switch") = 1;
        audio.start(sampler.rate, Sampler::periodSize, 32, 2);
        audioThread.spawn();
     }
    ~Music() {
        decodeThread.wait(); // ~Thread
        audioThread.wait(); // ~Thread
    }

     void setTitle(string title) {
         this->title = copyRef(title);
         pages = unique<Scroll<HList<GraphicsWidget>>>( apply(decodePDF(readFile(title+".pdf"_, folder), fonts), [](Graphics& o) { return GraphicsWidget(move(o)); }) );
         pages->horizontal = true;
         window.widget = window.focus = &pages->area();
         window.render();
         window.setTitle(title);
     }
     void nextTitle() {
         array<String> files = folder.list(Files|Sorted);
         for(size_t index: range(files.size-1)) if(startsWith(files[index], title) && !startsWith(files[index+1], title)) { setTitle(section(files[index+1],'.')); break; }
     }
} app;
