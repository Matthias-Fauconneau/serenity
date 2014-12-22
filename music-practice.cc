/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "midi-input.h"
#include "sampler.h"
#include "asound.h"
#include "pdf-renderer.h"
#include "window.h"

/// SFZ sampler and PDF renderer (tested with Salamander)
struct Music {
    Folder folder {"Scores"_, home()};
    const uint rate = 48000;
    Thread decodeThread;
    Sampler sampler {rate, "/Samples/Blanchet.sfz"_, [](uint){}, decodeThread}; // Audio mixing (consumer thread) preempts decoder running in advance (in producer thread (main thread))
    Thread audioThread;
    AudioOutput audio {{&sampler, &Sampler::read32}, audioThread};
    //MidiInput input;

    PDF pdf {readFile(arguments()[0], folder)};
    Window window {&pdf};

    Music() {
        //input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        decodeThread.spawn();
        //audio.start(sampler.rate, Sampler::periodSize, 32, 2);
        audioThread.spawn();
        AudioControl("Master Playback Switch") = 1;
        AudioControl("Headphone Jack") = 1;
    }
} app;
