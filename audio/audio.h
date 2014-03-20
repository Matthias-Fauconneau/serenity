#pragma once
/// \file audio.h PCM audio output interface
#include "thread.h"
#include "vector.h"
#include "function.h"

#define ASOUND 1
#if ASOUND
struct AudioOutput : /*Device,*/ Poll {
    struct _snd_pcm* pcm=0;
    bool running = false;
#else

struct Status { int state, pad; ptr hwPointer; long sec,nsec; int suspended_state; };
struct Control { ptr swPointer; long availableMinimum; };
#if !MMAP
struct SyncPtr {
    uint flags;
    union { Status status; byte pad1[64]; };
    union { Control control; byte pad2[64]; };
};
#endif

/// Audio output through ALSA PCM interface
struct AudioOutput : Device, Poll {
    Map maps[3];
    void* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
#if !MMAP
    SyncPtr syncPtr;
#endif
#endif
    static constexpr uint channels = 2;
    uint sampleBits = 0;
    uint rate = 0;
    uint periodSize = 0, bufferSize = 0;

    AudioOutput(function<uint(const mref<short2>& output)> read, Thread& thread=mainThread);
    virtual ~AudioOutput() { stop(); }

    /// Configures PCM for 16bit output
    /// \note \a read will be called back periodically to request an \a output frame of \a size samples
    void start(uint rate, uint periodSize, uint sampleBits=16);

    /// Drains audio output and stops requiring data from \a read callback
    void stop();

    /// Callback for poll events
    void event() override;

    function<uint(const mref<short2>&)> read16 = [](const mref<short2>&){return 0;};
};

#if !ASOUND
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
    AudioInput(function<uint(const ref<int2>& output)> write, uint rate=0, uint periodSize=0, Thread& thread=mainThread):
    AudioInput(32,rate,periodSize,thread) { write32=write; }
    /// Drains audio input and stops providing data to \a write callback
    virtual ~AudioInput();

    /// Callback for poll events
    void event();

private:
    function<uint(const ref<int2>& output)> write32 = [](const ref<int2>&){return 0;};

    Map maps[3];
    void* buffer = 0;
    const struct Status* status = 0;
    struct Control* control = 0;
#if !MMAP
    SyncPtr syncPtr;
#endif
};
#endif
