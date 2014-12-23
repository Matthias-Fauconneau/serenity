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
    const uint rate = 48000;
    Thread decodeThread;
    Sampler sampler {rate, "/Samples/Blanchet.sfz"_, [](uint){}, decodeThread}; // Audio mixing (consumer thread) preempts decoder running in advance (in producer thread (main thread))
    Thread audioThread;
    AudioOutput audio {{&sampler, &Sampler::read32}, audioThread};
    //MidiInput input;

    array<unique<Font>> fonts;
    Scroll<HList<GraphicsWidget>> pages = apply(decodePDF(readFile(arguments()[0], folder), fonts), [](Graphics& o) { return GraphicsWidget(move(o)); });
    Window window {&pages, int2(0, 768)};

    Music() {
        pages.horizontal = true;
        //input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        decodeThread.spawn();
        //audio.start(sampler.rate, Sampler::periodSize, 32, 2);
        audioThread.spawn();
        AudioControl("Master Playback Switch") = 1;
        //AudioControl("Headphone Jack") = 1;
    }
} app;
