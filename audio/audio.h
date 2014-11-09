#pragma once
/// \file audio.h PCM audio output interface
#include "thread.h"
#include "vector.h"
#include "function.h"

bool playbackDeviceAvailable();

enum State { Open, Setup, Prepared, Running, XRun, Draining, Paused, Suspended };
struct Status { int state, pad; ptr hwPointer; long sec,nsec; int suspended_state; };
struct Control { ptr swPointer; long availableMinimum; };

/// Audio output using ALSA PCM interface
struct AudioOutput : Device, Poll {
    Map maps[3];
    void* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;

    static constexpr uint channels = 2;
    uint sampleBits = 0;
    uint rate = 0;
    uint periodSize = 0, bufferSize = 0;

	function<size_t(mref<short2>)> read16 = [](mref<short2>){return 0;};
	function<size_t(mref<int2>)> read32 = [](mref<int2>){return 0;};

	AudioOutput(decltype(read16) read, Thread& thread=mainThread);
	AudioOutput(decltype(read32) read, Thread& thread=mainThread);
    virtual ~AudioOutput() { if(status) stop(); }
    explicit operator bool() const { return status; }

    /// Configures PCM for 16bit output
    /// \note \a read will be called back periodically to request an \a output frame of \a size samples
	void start(uint rate, uint periodSize, uint sampleBits);

    /// Drains audio output and stops requiring data from \a read callback
    void stop();

    /// Callback for poll events
    void event() override;
};

/// Audio input using ALSA PCM interface
struct AudioInput : Device, Poll {
    uint sampleBits = 0;
    uint channels = 2, rate = 0;
    uint periodSize = 0, bufferSize = 0;
    uint periods =0, overruns = 0;

    /// Configures PCM input
    AudioInput(uint sampleBits, uint rate, uint periodSize, Thread& thread);
    /// Configures PCM for 32bit input
    /// \note read will be called back periodically to provide an \a input frame of \a size samples
    /// \note 0 means maximum
    AudioInput(function<uint(const ref<int2> output)> write, uint rate=0, uint periodSize=0, Thread& thread=mainThread):
    AudioInput(32,rate,periodSize,thread) { write32=write; }
    /// Drains audio input and stops providing data to \a write callback
    virtual ~AudioInput();

    /// Callback for poll events
    void event();

private:
    function<uint(const ref<int2> output)> write32 = [](const ref<int2>){return 0;};

    Map maps[3];
    void* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
};

/// Audio control using ALSA Control interface
struct AudioControl : Device {
    uint id = 0;
    long min, max;
    AudioControl();
    operator long();
    void operator =(long value);
};
