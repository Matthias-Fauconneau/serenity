#include "ffmpeg.h"
#include "stretch.h"
#include "asound.h"
#include "sequencer.h"
#include "sampler.h"
#include "spectrogram.h"
#include "keyboard.h"
#include "layout.h"
#include "window.h"

struct AudioStretchFile : AudioFile, AudioStretch {
    AudioStretchFile(const ref<byte>& path):AudioFile(path),AudioStretch(rate){}
    uint need(int16 *data, uint size) override { return AudioFile::read(data,size); }
};

struct MusicAnalyzer {
    // Audio
    AudioStretchFile file __("/Music/StarCraft II/01 Wings Of Liberty.mp3"_);
    Spectrogram spectrogram __(16384, file.rate, 16);

    static constexpr uint periodSize = 1024;
    static constexpr uint N = Spectrogram::T*periodSize;

    //Thread thread __(-20); // Audio thread
    AudioOutput output __({this, &MusicAnalyzer::read}, file.rate, periodSize); //, thread);
    Sequencer input; // __(thread);
    Sampler sampler;

    int16* buffer=0;
    uint index=0;

    // Interface
    Keyboard keyboard;
    VBox layout;
    Window window __(&layout,int2(0,spectrogram.sizeHint().y+keyboard.sizeHint().y),"Music Analyzer"_);

    MusicAnalyzer() {
        assert(file.rate == output.rate);

        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);
        layout << &spectrogram << &keyboard;

        sampler.open("/Samples/Boesendorfer.sfz"_);
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);

        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
        keyboard.contentChanged.connect(&window,&Window::render);

        output.start();
        //thread.spawn();
    }
    ~MusicAnalyzer() { unallocate(buffer, N*2); }

    /// Playback files and mixes sampler (audio thread)
    uint read(int32* output, uint size) {
        if(!buffer) { // Fill buffer
            buffer = allocate<int16>(N*2);
            for(uint t=0; t<Spectrogram::T; t++) {
                int16* period = buffer+t*periodSize*2;
                uint read = file.AudioStretch::read(period, periodSize);
                assert(read == periodSize);
                spectrogram.write(period, read); // display file spectrogram without delay
            }
        }

        assert(size==periodSize);
        // Sampler output
        sampler.read(output, size);
        // Ring buffer output
        for(uint i: range(size*2)) output[i] += buffer[index+i]<<(16-3); //Mix delayed file with sampler output
        // Ring buffer input
        uint read = file.AudioStretch::read(buffer+index, size);
        assert(read == periodSize);
        spectrogram.write(buffer+index, size); // display file spectrogram without delay
        // Ring buffer update
        index += size*2;
        if(index == N*2) index=0;

        window.render();
        return read;
    }
} test;
