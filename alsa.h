#include "process.h"
#include "function.h"
#include "asound.h" //TODO: cleanup and inline

struct AudioOutput : Poll {
    function<void(int16* output, uint size)> read;
    pcm pcm;
    uint period;
    int frequency=48000, channels=2;
    bool running=false;

    /// Configures default PCM output
    /// \note If \a realtime is set, the lowest latency will be used (most wakeups)
    AudioOutput(function<void(int16* output, uint size)> read, bool realtime=false);
    void event(const pollfd&);
    virtual void start();
    virtual void stop();
};
