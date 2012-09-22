#pragma once
#include "process.h"
#include "function.h"

struct AudioOutput : Device, Poll {
    const uint channels=2, rate=48000; uint periodSize = 0, bufferSize = 0;
    Map maps[3];
    int16* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
    function<bool(ptr& swPointer, int16* output, uint size)> read;

    /// Configures PCM output
    /// \note read will be called back periodically to fill \a output with \a size samples
    /// \note if \a realtime is set, \a read will be called from a separate thread
    AudioOutput(function<bool(ptr& swPointer, int16* output, uint size)> read, Thread& thread=defaultThread, bool realtime=false);
    /// Starts audio output, will require data periodically from \a read callback
    void start();
    /// Drains audio output and stop requiring data from \a read callback
    void stop();
    /// Callback for poll events
    void event();
};
