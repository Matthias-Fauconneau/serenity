#pragma once
/// \file asound.h ALSA PCM output interface
#include "process.h"
#include "function.h"

/// Audio output through ALSA PCM interface
struct AudioOutput : Device, Poll {
    /// Configures PCM output
    /// \note read will be called back periodically to fill \a output with \a size samples
    /// \note if \a realtime is set, \a read will be called from a separate thread
    AudioOutput(function<bool(int32* output, uint size)> read, Thread& thread=mainThread(), bool realtime=false);
    /// Starts audio output, will require data periodically from \a read callback
    void start();
    /// Drains audio output and stop requiring data from \a read callback
    void stop();
    /// Callback for poll events
    void event();

    const uint channels = 2, rate = 44100;
    uint periodSize, bufferSize;
private:
    Map maps[3];
    int32* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
    function<bool(int32* output, uint size)> read;
};
