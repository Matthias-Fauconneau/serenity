#pragma once
#include "process.h"
#include "function.h"

struct pcm {
    int fd;
    bool running;
    uint buffer_size;
    uint boundary;
    uint channels;
    uint rate;
    uint period_size;
    uint period_count;
    struct mmap_status* mmap_status;
    struct mmap_control* mmap_control;
    void *mmap_buffer;
};

struct AudioOutput : Poll {
    function<void(int16* output, uint size)> read;
    pcm pcm;
    uint period=1024;
    int frequency=48000, channels=2;
    bool running=false;

    /// Configures default PCM output
    /// \note If \a realtime is set, the lowest latency will be used (most wakeups)
    AudioOutput(function<void(int16* output, uint size)> read, bool realtime=false);
    ~AudioOutput();
    void event(const pollfd&);
    virtual void start();
    virtual void stop();
};
