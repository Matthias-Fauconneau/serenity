#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
//#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <endian.h>
#include <sys/poll.h>
#include <errno.h>
#include <stdarg.h>

#include <alsa/asoundef.h>
#include <alsa/version.h>
#include <alsa/global.h>
#include <alsa/input.h>
#include <alsa/output.h>
#include <alsa/error.h>
#include <alsa/conf.h>
#include <alsa/pcm.h>
#include <alsa/rawmidi.h>
#include <alsa/timer.h>
#include <alsa/hwdep.h>
#include <alsa/control.h>
#include <alsa/mixer.h>
#include <alsa/seq_event.h>
#include <alsa/seq.h>
#include <alsa/seqmid.h>
#include <alsa/seq_midi_event.h>

#include "media.h"
#include "interface.h"

struct ALSA : AudioOutput, Poll {
    snd_pcm_t* pcm=0;
    snd_pcm_uframes_t period;
    AudioInput* input=0;
    AudioFormat format{48000,2};

	ALSA() : Poll(false) {
        snd_pcm_open(&pcm,"default",SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK|SND_PCM_NO_SOFTVOL);
        snd_pcm_hw_params_t *hw; snd_pcm_hw_params_alloca(&hw); snd_pcm_hw_params_any(pcm,hw);
        snd_pcm_hw_params_set_access(pcm,hw, SND_PCM_ACCESS_MMAP_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm,hw, SND_PCM_FORMAT_S16);
        snd_pcm_hw_params_set_rate(pcm,hw, 48000, 0);
        snd_pcm_hw_params_set_channels(pcm,hw, 2);
		//snd_pcm_hw_params_set_period_size_first(pcm, hw, &period, 0);
		//snd_pcm_uframes_t bufferSize=period*3; snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &bufferSize);
		snd_pcm_hw_params_set_period_size_last(pcm, hw, &period, 0);
		snd_pcm_uframes_t bufferSize; snd_pcm_hw_params_set_buffer_size_last(pcm, hw, &bufferSize);
        snd_pcm_hw_params(pcm, hw);
        snd_pcm_sw_params_t *sw; snd_pcm_sw_params_alloca(&sw);
        snd_pcm_sw_params_current(pcm,sw);
        snd_pcm_sw_params_set_avail_min(pcm,sw, period);
		snd_pcm_sw_params_set_period_event(pcm,sw, 1);
        snd_pcm_sw_params(pcm,sw);
	}
	void setInput(AudioInput* input) { this->input=input; input->setup(format); }
    pollfd poll() { pollfd p; snd_pcm_poll_descriptors(pcm,&p,1); return p; }
	void start() { registerPoll(); }
	void stop() { snd_pcm_drain(pcm); unregisterPoll(); }
    bool event(pollfd p) {
		assert(input);
        unsigned short revents;
        snd_pcm_poll_descriptors_revents(pcm, &p, 1, &revents);
        if(!(revents & POLLOUT)) return true;
		if( snd_pcm_state(pcm) == SND_PCM_STATE_XRUN ) { snd_pcm_prepare(pcm); }
		snd_pcm_uframes_t frames = (snd_pcm_uframes_t)snd_pcm_avail_update(pcm); //snd_pcm_avail(pcm);
        assert(frames >= period,"snd_pcm_avail");
        const snd_pcm_channel_area_t* areas; snd_pcm_uframes_t offset;
        frames=period;
        snd_pcm_mmap_begin(pcm, &areas, &offset, &frames);
        assert(frames == period,"snd_pcm_mmap_begin");
        int16* output = (int16*)areas[0].addr+offset*2;
		input->read(output,(int)period);
		snd_pcm_mmap_commit(pcm, offset, frames);
		if(snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED && snd_pcm_avail_update(pcm)==0) snd_pcm_start(pcm);
        return true;
    }
};
AudioOutput* AudioOutput::instance() { return new ALSA(); }
