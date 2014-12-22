/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "midi-input.h"
#include "sampler.h"
#include "asound.h"

/// SFZ sampler and PDF renderer (tested with Salamander)
struct Music {
    //Folder folder{"Scores"_, home()};
    const uint rate = 48000;
    Sampler sampler {rate, "/Samples/Salamander.sfz"_, [](uint){}}; // Audio mixing (consumer thread) preempts decoder running in advance (in producer thread (main thread))
    Thread thread;
    AudioOutput audio {{&sampler, &Sampler::read32}, thread};
    MidiInput input;

    Music() {
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        audio.start(sampler.rate, Sampler::periodSize, 32, 2);
        thread.spawn();
    }
} app;
