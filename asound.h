#pragma once
#include "process.h"
#include "function.h"

struct AudioOutput : Device, Poll {
    const uint channels=2, rate=48000; uint periodSize = 0, bufferSize = 0;
    int16* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
    function<bool(int16* output, uint size)> read;

    /// Configures PCM output
    /// \note If \a realtime is set, the lowest latency will be used (most wakeups)
    AudioOutput(function<bool(int16* output, uint size)> read, bool realtime=false);
    ~AudioOutput();
    void event();
    void start();
    void stop();
};
