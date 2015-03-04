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

    uint channels = 0;
    uint sampleBits = 0;
    uint rate = 0;
    uint periodSize = 0, bufferSize = 0;
    uint underruns = 0;

    function<size_t(mref<short2>)> read16 = [](mref<short2>){ error("read16"); return 0;};
    function<size_t(mref<int2>)> read32 = [](mref<int2>){ error("read32"); return 0;};

    AudioOutput(Thread& thread=mainThread);
    AudioOutput(decltype(read16) read, Thread& thread=mainThread);
    AudioOutput(decltype(read32) read, Thread& thread=mainThread);
    virtual ~AudioOutput() { if(status) stop(); }
    explicit operator bool() const { return status; }

    /// Configures PCM for 16bit output
    /// \note \a read will be called back periodically to request an \a output frame of \a size samples
    bool start(uint rate, uint periodSize, uint sampleBits, uint channels);

    /// Drains audio output and stops requiring data from \a read callback
    void stop();

    /// Callback for poll events
    void event() override;
};

/// Audio input using ALSA PCM interface
struct AudioInput : Device, Poll {
    Lock lock;

    const uint sampleBits = 32;
    uint channels = 0, rate = 0;
    uint periodSize = 0, bufferSize = 0;
    uint periods = 0, overruns = 0;
    uint time = 0;

    /// Configures PCM input
    AudioInput(Thread& thread);
    /// Configures PCM for 32bit input
    /// \note read will be called back periodically to provide an \a input frame of \a size samples
    /// \note 0 means maximum
    AudioInput(function<uint(const ref<int32> output)> write32, Thread& thread=mainThread) : AudioInput(thread) { this->write32=write32; }
    /// Drains audio input and stops providing data to \a write callback
    virtual ~AudioInput() { if(status) stop(); }
    explicit operator bool() const { return status; }

    void setup(uint channels, uint rate, uint periodSize);
    void start();
    void start(uint channels, uint rate, uint periodSize) { setup(channels, rate, periodSize); start(); }
    void stop();

    /// Callback for poll events
    void event();

private:
    function<uint(const ref<int32> output)> write32;

    Map maps[3];
    void* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
};

/// Audio control using ALSA Control interface
struct AudioControl : Device {
    uint id = 0;
    long min, max;
    AudioControl(string name = "Master Playback Volume");
    operator long();
    void operator =(long value);
};
