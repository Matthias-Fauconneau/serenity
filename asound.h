#pragma once
#include "process.h"
#include "function.h"

struct AudioOutput : Poll {
    int fd = 0;
    uint channels=2, rate=48000, periodCount=2, periodSize=8192, bufferSize = 0;
    int16* buffer = 0;
    struct Status* status = 0;
    struct Control* control = 0;
    function<void(int16* output, uint size)> read;

    /// Configures default PCM output
    /// \note If \a realtime is set, the lowest latency will be used (most wakeups)
    AudioOutput(function<void(int16* output, uint size)> read) : read(read) {};
    ~AudioOutput() { stop(); }
    void event(const pollfd&);
    void start(bool realtime=false);
    void stop();
};
