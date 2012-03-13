#include "alsa.h"

#include <sys/types.h>
#include <alsa/global.h>
struct snd_input_t;
struct snd_output_t;
#include <alsa/conf.h>
#include <alsa/pcm.h>
#include <alsa/timer.h>
#include <alsa/control.h>
#include <poll.h>

AudioOutput::AudioOutput(bool realtime) {
    snd_pcm_open(&pcm,"default",SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK|SND_PCM_NO_SOFTVOL);
    byte alloca_hw[snd_pcm_hw_params_sizeof()];
    snd_pcm_hw_params_t* hw=(snd_pcm_hw_params_t*)alloca_hw; snd_pcm_hw_params_any(pcm,hw);
    snd_pcm_hw_params_set_access(pcm,hw, SND_PCM_ACCESS_MMAP_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm,hw, SND_PCM_FORMAT_S16);
    snd_pcm_hw_params_set_rate(pcm,hw, 48000, 0);
    snd_pcm_hw_params_set_channels(pcm,hw, 2);
    snd_pcm_hw_params_set_period_size_first(pcm, hw, &period, 0);
    snd_pcm_uframes_t bufferSize;
    if(realtime) snd_pcm_hw_params_set_buffer_size_first(pcm, hw, &bufferSize);
    else snd_pcm_hw_params_set_buffer_size_last(pcm, hw, &bufferSize);
    snd_pcm_hw_params(pcm, hw);
    byte alloca_sw[snd_pcm_sw_params_sizeof()];
    snd_pcm_sw_params_t *sw=(snd_pcm_sw_params_t*)alloca_sw;
    snd_pcm_sw_params_current(pcm,sw);
    snd_pcm_sw_params_set_avail_min(pcm,sw, period);
    snd_pcm_sw_params_set_period_event(pcm,sw, 1);
    snd_pcm_sw_params(pcm,sw);
}
void AudioOutput::start() { if(running) return; pollfd p; snd_pcm_poll_descriptors(pcm,&p,1); registerPoll(p); running=true; }
void AudioOutput::stop() { if(!running) return;  snd_pcm_drain(pcm); running=false; }
void AudioOutput::event(pollfd p) {
    assert(read.method);
    unsigned short revents;
    snd_pcm_poll_descriptors_revents(pcm, &p, 1, &revents);
    if(!(revents & POLLOUT)) return;
    if( snd_pcm_state(pcm) == SND_PCM_STATE_XRUN ) { log("xrun"_); snd_pcm_prepare(pcm); }
    snd_pcm_uframes_t frames = (snd_pcm_uframes_t)snd_pcm_avail_update(pcm); //snd_pcm_avail(pcm);
    assert(frames >= period);
    const snd_pcm_channel_area_t* areas; snd_pcm_uframes_t offset;
    frames=period;
    snd_pcm_mmap_begin(pcm, &areas, &offset, &frames);
    assert(frames == period);
    int16* output = (int16*)areas[0].addr+offset*2;
    read(output,(int)period);
    snd_pcm_mmap_commit(pcm, offset, frames);
    if(snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED && snd_pcm_avail_update(pcm)==0) snd_pcm_start(pcm);
    return;
}
