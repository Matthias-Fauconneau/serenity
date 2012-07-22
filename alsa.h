#include "process.h"
#include "signal.h"

typedef struct _snd_pcm snd_pcm_t;
typedef unsigned long snd_pcm_uframes_t;
struct AudioOutput : Poll {
    snd_pcm_t* pcm=0;
    snd_pcm_uframes_t period;
    int frequency=48000, channels=2;
    bool running=false;

    signal<void(int16* output, uint size)> read;

    AudioOutput(bool realtime=false);
    void event(pollfd);
    virtual void start();
    virtual void stop();
};
