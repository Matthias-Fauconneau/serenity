#pragma once
/// \file audio.h PCM audio output interface
#include "thread.h"
#include "function.h"

/// Audio output through ALSA PCM interface
struct AudioOutput : Device, Poll {
    uint sampleBits = 0;
    uint channels = 2, rate = 0;
    uint periodSize = 0, bufferSize = 0;

    /// Configures PCM output
    AudioOutput(uint sampleBits, uint rate, uint periodSize, Thread& thread);
    AudioOutput(function<uint(int16* output, uint size)> read, uint rate=48000, uint periodSize=8192, Thread& thread=mainThread):
	AudioOutput(16,rate,periodSize,thread) { read16=read; }
    /// Configures PCM for 32bit output
    /// \note read will be called back periodically to fill \a output with \a size samples
    AudioOutput(function<uint(int32* output, uint size)> read, uint rate=48000, uint periodSize=8192, Thread& thread=mainThread):
	AudioOutput(32,rate,periodSize,thread) { read32=read; }

    /// Starts audio output, will require data periodically from \a read callback
    void start();
    /// Drains audio output and stops requiring data from \a read callback
    void stop();
    /// Callback for poll events
    void event();

private:
    function<uint(int16* output, uint size)> read16 = [](int16*,uint){return 0;};
    function<uint(int32* output, uint size)> read32 = [](int32*,uint){return 0;};

    Map maps[3];
    void* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
};

struct AudioInput : Device, Poll {
    uint sampleBits = 0;
    uint channels = 2, rate = 0;
    uint periodSize = 0, bufferSize = 0;

    /// Configures PCM input
    AudioInput(uint sampleBits, uint rate, uint periodSize, Thread& thread);
    AudioInput(function<uint(int16* output, uint size)> write, uint rate=48000, uint periodSize=8192, Thread& thread=mainThread):
    AudioInput(16,rate,periodSize,thread) { write16=write; }

    /// Starts audio input, will provide data periodically through \a write callback
    void start();
    /// Drains audio input and stops providng data through \a write callback
    void stop();
    /// Callback for poll events
    void event();

private:
    function<uint(int16* output, uint size)> write16 = [](int16*,uint){return 0;};

    Map maps[3];
    void* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
};
