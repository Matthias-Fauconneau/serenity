#include "process.h"
#include "ffmpeg.h"
#include "stretch.h"
#include "asound.h"
#include "sequencer.h"
#include "sampler.h"
#include "spectrogram.h"
#include "keyboard.h"
#include "layout.h"
#include "window.h"

#if STRETCH
struct AudioStretchFile : AudioFile, AudioStretch {
    AudioStretchFile(const ref<byte>& path):AudioFile(path),AudioStretch(rate){}
    uint need(int16 *data, uint size) override { return AudioFile::read(data,size); }
    uint read(int16* data, uint size) { return AudioStretch::read(data,size); }
};
#else
typedef AudioFile AudioStretchFile;
#endif

struct Analyzer {
    // Audio
    AudioStretchFile file __("/root/Documents/StarCraft 2 - Theme Song.m4a"_);
    Spectrogram spectrogram __(16384, file.rate, 16);

    static constexpr uint periodSize = 1024;
    static constexpr uint N = Spectrogram::T*periodSize;

    //Thread thread __(-20); // Audio thread
    AudioOutput output __({this, &Analyzer::read}, file.rate, periodSize /*, thread*/);
    Sequencer input __(/*thread*/);
    Sampler sampler;

    int16* buffer = allocate<int16>(N*2);
    uint readIndex=0;
    uint writeIndex=0;

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
        assert(file.rate == output.rate);

        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(Key(' ')).connect(this, &Analyzer::togglePlay);

        elapsed.minSize.x=remaining.minSize.x=64;
        slider.valueChanged.connect(this, &Analyzer::seek);
        toolbar << &elapsed << &slider << &remaining;
        layout << &toolbar << &spectrogram << &keyboard;

        sampler.open("/Samples/Boesendorfer.sfz"_);
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);

        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
        keyboard.contentChanged.connect(&window,&Window::render);

        output.start();
        //thread.spawn();
    }
    ~Analyzer() { unallocate(buffer); }

    bool playing=true;
    void togglePlay() { setPlaying(!playing); }
    void setPlaying(bool play) {
        if(play) { output.start(); window.setIcon(playIcon()); }
        else { output.stop(); window.setIcon(pauseIcon()); }
        playing=play; window.render();
    }
    Lock bufferLock;
    void seek(int position) {
        output.stop();
        file.seek(position); update(file.position(),file.duration()); window.render();
        Locker lock(bufferLock);
        writeIndex=readIndex;
        output.start();
    }
    void update(int position, int duration) {
        if(slider.value == position) return;
        if(!window.mapped) return;
        slider.value = position; slider.maximum=duration;
        elapsed.setText(string(dec(uint64(position/60),2)+":"_+dec(uint64(position%60),2)));
        remaining.setText(string(dec(uint64((duration-position)/60),2)+":"_+dec(uint64((duration-position)%60),2)));
    }

    /// Playback files and mixes sampler (audio thread)
    uint read(int32* output, uint size) {
        update(file.position(),file.duration());
        assert(size==periodSize);

        // Spectrogram
        Locker lock(bufferLock);
        uint read = file.read(buffer+(writeIndex%N)*2, size);
        spectrogram.write(buffer+(writeIndex%N)*2, read); // Displays spectrogram without delay
        writeIndex += size;

        while(writeIndex<readIndex+N/2) { // Transforms faster to fill up spectrogram
            uint read = file.read(buffer+(writeIndex%N)*2, size);
            spectrogram.write(buffer+(writeIndex%N)*2, read); // Displays spectrogram without delay
            writeIndex += size;
        }

        window.render();

        // Audio output
        sampler.read(output, size);
        for(uint i: range(size*2)) output[i] += buffer[(readIndex%N)*2+i]<<(16-3); //Mix delayed file with sampler output
        readIndex += size;

        return size;
    }
} test;
