#include "process.h"
#include "function.h"

typedef struct _snd_pcm snd_pcm_t;
typedef unsigned long snd_pcm_uframes_t;
struct AudioOutput : Poll {
    function<void(int16* output, uint size)> read;
    snd_pcm_t* pcm=0;
    snd_pcm_uframes_t period;
    int frequency=48000, channels=2;
    bool running=false;

    AudioOutput(function<void(int16* output, uint size)> read, bool realtime=false);
    void event(pollfd);
    virtual void start();
    virtual void stop();
};
