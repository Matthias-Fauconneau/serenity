#pragma once
/// \file asound.h ALSA PCM output interface
#include "process.h"
#include "function.h"

/// Audio output through ALSA PCM interface
struct AudioOutput : Device, Poll {
    typedef int16 sample;
    static constexpr uint sampleBits = sizeof(sample)*8;
    static constexpr int sampleRange = 1<<(sizeof(sample)*8-1);
    uint channels = 2, rate;
    uint periodSize, bufferSize;

    /// Configures PCM output
    /// \note read will be called back periodically to fill \a output with \a size samples
    /// \note if \a realtime is set, \a read will be called from a separate thread
    AudioOutput(function<bool(AudioOutput::sample* output, uint size)> read, uint rate=44100, uint periodSize=4096, Thread& thread=mainThread());
    /// Starts audio output, will require data periodically from \a read callback
    void start();
    /// Drains audio output and stop requiring data from \a read callback
    void stop();
    /// Callback for poll events
    void event();

private:
    Map maps[3];
    sample* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
    function<bool(sample* output, uint size)> read;
};
