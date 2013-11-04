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
    /// Configures PCM for 16bit output
    /// \note \a read will be called back periodically to request an \a output frame of \a size samples
    /// \note 0 means maximum
    AudioOutput(function<uint(int16* output, uint size)> read, uint rate=0, uint periodSize=0, Thread& thread=mainThread):
	AudioOutput(16,rate,periodSize,thread) { read16=read; }
    /// Configures PCM for 32bit output
    /// \note \a read will be called back periodically to request an \a output frame of \a size samples
    /// \note 0 means maximum
    AudioOutput(function<uint(int32* output, uint size)> read, uint rate=0, uint periodSize=0, Thread& thread=mainThread):
	AudioOutput(32,rate,periodSize,thread) { read32=read; }
    /// Configures PCM for either 16bit or 32bit output depending on driver capability
    /// \note read will be called back periodically to provide an \a input frame of \a size samples
    /// \note 0 means maximum
    AudioOutput(function<uint(int16* output, uint size)> read16, function<uint(int32* output, uint size)> read32,
                uint rate=0, uint periodSize=0, Thread& thread=mainThread):
    AudioOutput(0,rate,periodSize,thread) { this->read16=read16; this->read32=read32; }

    /// Starts audio output, will require data periodically from \a read callback
    void start();
    /// Drains audio output and stops requiring data from \a read callback
    void stop();
    /// Callback for poll events
    void event();

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
    uint periods =0, overruns = 0;

    /// Configures PCM input
    AudioInput(uint sampleBits, uint rate, uint periodSize, Thread& thread);
    AudioInput(function<uint(const int16* output, uint size)> write, uint rate=0, uint periodSize=0, Thread& thread=mainThread):
    /// Configures PCM for 16bit input
    /// \note write will be called back periodically to provide an \a input frame of \a size samples
    /// \note 0 means maximum
    AudioInput(16,rate,periodSize,thread) { write16=write; }
    /// Configures PCM for 32bit input
    /// \note read will be called back periodically to provide an \a input frame of \a size samples
    /// \note 0 means maximum
    AudioInput(function<uint(const int32* output, uint size)> write, uint rate=0, uint periodSize=0, Thread& thread=mainThread):
    AudioInput(32,rate,periodSize,thread) { write32=write; }
    /// Configures PCM for either 16bit or 32bit input depending on driver capability
    /// \note read will be called back periodically to provide an \a input frame of \a size samples
    /// \note 0 means maximum
    AudioInput(function<uint(const int16* output, uint size)> write16, function<uint(const int32* output, uint size)> write32,
               uint rate=0, uint periodSize=0, Thread& thread=mainThread):
    AudioInput(0,rate,periodSize,thread) { this->write16=write16; this->write32=write32; }

    /// Starts audio input, will provide data periodically through \a write callback
    void start();
    /// Drains audio input and stops providng data through \a write callback
    void stop();
    /// Callback for poll events
    void event();

private:
    function<uint(const int16* output, uint size)> write16 = [](const int16*,uint){return 0;};
    function<uint(const int32* output, uint size)> write32 = [](const int32*,uint){return 0;};

    Map maps[3];
    void* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
};
