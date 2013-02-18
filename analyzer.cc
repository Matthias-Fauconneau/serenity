#include "process.h"
#include "sequencer.h"
#include "sampler.h"
#include "ffmpeg.h"
#include "spectrogram.h"
#include "keyboard.h"
#include "layout.h"
#include "window.h"
#include "interface.h"

#if AUDIO
#include "asound.h"
#else
#include "time.h"
#endif

struct Analyzer {
    static constexpr uint periodSize = 1024; //STFT frame size
    Audio<float> audio = decodeAudio<float>(Map("/root/Documents/StarCraft 2 - Theme Song.m4a"_));
    Spectrogram spectrogram __(audio, 1024, 16384);

    bool playing = true;
    uint position = 0;
    uint duration = audio.data.size / audio.channels / sizeof(float);

#if AUDIO
    Thread thread __(-20); // Audio thread
    AudioOutput output __({this, &Analyzer::read}, rate, periodSize, thread);
#else
    Timer timer;
#endif
#if SAMPLER
    Sequencer input __(thread);
    Sampler sampler;
#endif

    // Interface
    ICON(play) ICON(pause)
    Text elapsed __(string("00:00"_));
    Slider slider;
    Text remaining __(string("00:00"_));
    HBox toolbar;
    Keyboard keyboard;
    VBox layout;
    Window window __(&layout,int2(0,spectrogram.sizeHint().y+keyboard.sizeHint().y),"Music Analyzer"_);

    Analyzer() {

        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(Key(' ')).connect(this, &Analyzer::togglePlay);

        elapsed.minSize.x=remaining.minSize.x=64;
        slider.valueChanged.connect(this, &Analyzer::seek);
        toolbar << &elapsed << &slider << &remaining;
        layout << &toolbar << &spectrogram << &keyboard;

#if SAMPLER
        sampler.open("/Samples/Boesendorfer.sfz"_);
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
#endif
        keyboard.contentChanged.connect(&window,&Window::render);

#if AUDIO
        assert(file.rate == output.rate);
        output.start();
        thread.spawn();
#else
        timer.timeout.connect(this,&Analyzer::timeout);
        timeout();
#endif
    }

    void togglePlay() { setPlaying(!playing); }
    void setPlaying(bool play) {
        playing=play; window.render();
        if(play) {
#if AUDIO
            output.start();
#else
            timeout();
#endif
            window.setIcon(playIcon());
        }
        else {
#if AUDIO
            output.stop();
#endif
            window.setIcon(pauseIcon());
        }
    }

    void seek(int position) {
        this->position=position; //file.seek(position); update(file.position(),file.duration()); window.render(); TODO
    }

    void timeout() {
        if(!playing) return;
        timer.setRelative(periodSize*1000/audio.rate);
        int32 output[2*periodSize];
        read(output, periodSize);
    }

    void update(int position, int duration) {
        //position -= 3*spectrogram.N/4;
        if(slider.value == position) return;
        if(!window.mapped) return;
        slider.value = position; slider.maximum=duration;
        elapsed.setText(string(dec(uint64(position/60),2)+":"_+dec(uint64(position%60),2)));
        remaining.setText(string(dec(uint64((duration-position)/60),2)+":"_+dec(uint64((duration-position)%60),2)));
    }

    /// Playback files and mixes sampler (audio thread)
    uint read(int32* output, uint size) {
        update(position, duration);
        assert(size==periodSize);
        spectrogram.position = position / spectrogram.periodSize;

        window.render();

        // Audio output
#if SAMPLER
        sampler.read(output, size);
#else
         for(uint i: range(size*2)) output[i] = 0;
#endif
        for(uint i: range(size*2)) output[i] += audio.data[position*2+i]*0x1.0p29f; //Mix delayed file with sampler output

        position += size;
        return size;
    }
} test;
