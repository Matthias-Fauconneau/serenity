#include "alsa.h"
#include "linux.h"
#include "debug.h"

#include "asound.h"

AudioOutput::AudioOutput(function<void(int16* output, uint size)> read, bool unused realtime) : read(read) {
    pcm_config config = __(.channels=2, .rate=48000, .format=PCM_FORMAT_S16_LE, .period_size=0, .period_count=0);
    pcm=pcm_open(0,0,PCM_OUT|PCM_MMAP,&config);
}
void AudioOutput::start() { if(running) return; registerPoll(__(pcm.fd,POLLOUT)); running=true; }
void AudioOutput::stop() { if(!running) return; unregisterPoll(); pcm_drain(pcm); running=false; }
void AudioOutput::event(const pollfd& p) {
    if(!(p.revents & POLLOUT)) { warn(p.revents); return; }
    if( pcm_state(pcm) == PCM_STATE_XRUN ) { warn("xrun"_); pcm_prepare(pcm); }
    uint frames = pcm_mmap_avail(pcm); //snd_pcm_avail(pcm);
    assert(frames >= period);
    void* buffer; uint offset;
    frames=period;
    pcm_mmap_begin(pcm, &buffer, &offset, &frames);
    assert(frames == period);
    int16* output = (int16*)buffer;
    read(output,(int)period);
    pcm_mmap_commit(pcm, offset, frames);
    if(pcm_state(pcm) == PCM_STATE_PREPARED && pcm_mmap_avail(pcm)==0) pcm_start(pcm);
    return;
}
