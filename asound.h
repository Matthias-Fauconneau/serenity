#pragma once
#include "process.h"
#include "function.h"

struct AudioOutput : Poll {
    int fd;
    bool running=false;
    uint channels=2, rate=48000, periodCount=2, periodSize=8192, bufferSize; //, boundary;
    int16* buffer;
    struct Status* status;
    struct Control* control;
    function<void(int16* output, uint size)> read;

    /// Configures default PCM output
    /// \note If \a realtime is set, the lowest latency will be used (most wakeups)
    AudioOutput(function<void(int16* output, uint size)> read, bool realtime=false);
    ~AudioOutput();
    void event(const pollfd&);
    void start();
    void stop();
};
