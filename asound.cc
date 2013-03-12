#include "asound.h"
#include "string.h"
#include "linux.h"

#if ASOUND
#include <sys/types.h>
#include <alsa/global.h>
struct snd_input_t;
struct snd_output_t;
#include <alsa/conf.h>
#include <alsa/pcm.h>
#include <alsa/timer.h>
#include <alsa/control.h>

AudioOutput::AudioOutput(uint sampleBits, uint rate, uint unused periodSize, Thread& thread) : Poll(0,POLLOUT,thread) {
    check( snd_pcm_open(&pcm,"default",SND_PCM_STREAM_PLAYBACK,SND_PCM_NO_SOFTVOL|SND_PCM_NO_AUTO_RESAMPLE) );
    byte alloca_hw[snd_pcm_hw_params_sizeof()];
    snd_pcm_hw_params_t* hw=(snd_pcm_hw_params_t*)alloca_hw; snd_pcm_hw_params_any(pcm,hw);
    check( snd_pcm_hw_params_set_access(pcm,hw, SND_PCM_ACCESS_RW_INTERLEAVED) );
    check( snd_pcm_hw_params_set_format(pcm,hw, sampleBits==16?SND_PCM_FORMAT_S16_LE:SND_PCM_FORMAT_S32_LE) );
    snd_pcm_format_t format=SND_PCM_FORMAT_UNKNOWN; snd_pcm_hw_params_get_format(hw, &format);
    if(format==SND_PCM_FORMAT_S16_LE) this->sampleBits=16;
    else if(format==SND_PCM_FORMAT_S32_LE) this->sampleBits=32;
    else error((uint)format);
    check( snd_pcm_hw_params_set_channels(pcm,hw, 2) );
    check( snd_pcm_hw_params_set_rate(pcm,hw, rate, 0), rate );
    check( snd_pcm_hw_params_set_period_size(pcm, hw, periodSize, 0), periodSize);
    snd_pcm_uframes_t period_size; check( snd_pcm_hw_params_get_period_size(hw, &period_size, 0) );
    //snd_pcm_uframes_t period_size; check( snd_pcm_hw_params_set_period_size_first(pcm, hw, &period_size, 0) );
    this->periodSize=period_size;
    //check( snd_pcm_hw_params_set_periods(pcm, hw, 2, 0) );
    //uint periods=0; check( snd_pcm_hw_params_get_periods(hw, &periods, 0) );
    uint periods=0; check( snd_pcm_hw_params_set_periods_first(pcm, hw, &periods, 0) );
    bufferSize = periods * this->periodSize;
    check( snd_pcm_hw_params(pcm, hw) );
    byte alloca_sw[snd_pcm_sw_params_sizeof()];
    snd_pcm_sw_params_t *sw=(snd_pcm_sw_params_t*)alloca_sw;
    snd_pcm_sw_params_current(pcm, sw);
    snd_pcm_sw_params_set_avail_min(pcm, sw, this->periodSize);
    snd_pcm_sw_params(pcm,sw);
    log(this->periodSize, periods, bufferSize);
}
void AudioOutput::start() { if(running) return; snd_pcm_poll_descriptors(pcm,this,1); registerPoll(); running=true; }
void AudioOutput::stop() { if(!running) return; unregisterPoll(); snd_pcm_drain(pcm); running=false; }
void AudioOutput::event() {
    unsigned short revents;
    snd_pcm_poll_descriptors_revents(pcm, this, 1, &revents);
    if(!(revents & POLLOUT)) return;
    if( snd_pcm_state(pcm) == SND_PCM_STATE_XRUN ) { log("xrun"_); snd_pcm_prepare(pcm); }
    snd_pcm_uframes_t frames = (snd_pcm_uframes_t)snd_pcm_avail(pcm); //snd_pcm_avail_update(pcm); //
    if(frames>=periodSize) {
#if MMAP
        const snd_pcm_channel_area_t* areas; snd_pcm_uframes_t offset;
        frames=periodSize;
        snd_pcm_mmap_begin(pcm, &areas, &offset, &frames);
        assert(frames == periodSize);
        if(sampleBits==16) frames=read16((int16*)areas[0].addr+offset*channels, periodSize);
        else if(sampleBits==32) frames=read32((int32*)areas[0].addr+offset*channels, periodSize);
        else error(sampleBits);
        snd_pcm_mmap_commit(pcm, offset, frames);
#else
        buffer<byte> audio(periodSize*channels*sampleBits/8);
        if(sampleBits==16) frames=read16((int16*)audio.data, periodSize);
        else if(sampleBits==32) frames=read32((int32*)audio.data, periodSize);
        else error(sampleBits);
        snd_pcm_writei(pcm, audio.data, frames);
#endif
        if(snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED) snd_pcm_start(pcm);
    } else log(frames);
}

#else
enum State { Open, Setup, Prepared, Running, XRun, Draining, Paused };
enum Access { MMapInterleaved=0 };
enum Format { S16_LE=2, S32_LE=10 };
enum SubFormat { Standard=0 };
enum Masks { Access, Format, SubFormat };
enum Intervals { SampleBits, FrameBits, Channels, Rate, PeriodTime, PeriodSize, PeriodBytes, Periods, BufferTime, BufferSize };
enum Flags { NoResample=1, ExportBuffer=2, NoPeriodWakeUp=4 };
struct Interval {
    uint min, max; uint openmin:1, openmax:1, integer:1, empty:1, pad:28;
    Interval():min(0),max(-1),openmin(0),openmax(0),integer(0),empty(0),pad(0){}
    Interval(uint exact):min(exact),max(exact),openmin(0),openmax(0),integer(1),empty(0),pad(0){}
    operator uint() { assert(integer); assert(min==max); return max; }
};
struct Mask {
    int bits[8] = {~0,~0,0,0,0,0,0,0};
    Mask& set(uint bit) { assert(bit < 256); bits[0] = bits[1] = 0; bits[bit >> 5] |= (1 << (bit & 31)); return *this; }
};
struct HWParams {
    uint flags = NoResample;
    Mask masks[3];
    Mask mres[5];
    Interval intervals[12];
    Interval ires[9];
    uint rmask=0, cmask=0, info=0, msbits=0, rate_num=0, rate_den=0;
    long fifo_size=0;
    byte reserved[64]={};
    Interval& interval(int i) { assert(i<12); return intervals[i]; }
    Mask& mask(int i) { assert(i<3); return masks[i]; }
};
struct SWParams {
 int tstamp_mode=0;
 uint period_step=1, sleep_min=0;
 long avail_min=0, xfer_align=0, start_threshold=0, stop_threshold=0, silence_threshold=0, silence_size=0, boundary=0;
 byte reserved[64];
};
struct Status { int state, pad; ptr hwPointer; long sec,nsec; int suspended_state; };
struct Control { ptr swPointer; long availableMinimum; };

typedef IOWR<'A', 0x11,HWParams> HW_PARAMS;
typedef IOWR<'A', 0x13,SWParams> SW_PARAMS;
typedef IO<'A', 0x40> PREPARE;
typedef IO<'A', 0x42> START;
typedef IO<'A', 0x44> DRAIN;

AudioOutput::AudioOutput(uint sampleBits, uint rate, uint periodSize, Thread& thread)
    : Device("/dev/snd/pcmC0D0p"_,ReadWrite), Poll(Device::fd,POLLOUT,thread) {
    HWParams hparams;
    hparams.mask(Access).set(MMapInterleaved);
    hparams.mask(Format).set(sampleBits==16?S16_LE:S32_LE);
    hparams.mask(SubFormat).set(Standard);
    hparams.interval(SampleBits) = sampleBits;
    hparams.interval(FrameBits) = sampleBits*channels;
    hparams.interval(Channels) = channels;
    hparams.interval(Rate) = rate; assert(rate);
    hparams.interval(Periods) = 2;
    hparams.interval(PeriodSize) = periodSize;
    iowr<HW_PARAMS>(hparams);
    this->sampleBits = hparams.interval(SampleBits);
    this->rate = hparams.interval(Rate);
    this->periodSize = hparams.interval(PeriodSize);
    bufferSize = hparams.interval(Periods) * this->periodSize;
    buffer = (void*)((maps[0]=Map(Device::fd, 0, bufferSize * channels * this->sampleBits/8, Map::Write)).data);
    status = (Status*)((maps[1]=Map(Device::fd, 0x80000000, 0x1000, Map::Read)).data);
    control = (Control*)((maps[2]=Map(Device::fd, 0x81000000, 0x1000, Map::Read|Map::Write)).data);
}

void AudioOutput::start() { if(status->state != Prepared && status->state != Running) io<PREPARE>(); registerPoll(); }
void AudioOutput::stop() { if(status->state == Running) io<DRAIN>(); unregisterPoll(); }
void AudioOutput::event() {
    if(status->state == XRun) { log("Underrun"_); io<PREPARE>(); }
    int available = status->hwPointer + bufferSize - control->swPointer;
    if(available>=(int)periodSize) {
        uint readSize;
        if(sampleBits==16) readSize=read16(((int16*)buffer)+(control->swPointer%bufferSize)*channels, periodSize);
        else if(sampleBits==32) readSize=read32(((int32*)buffer)+(control->swPointer%bufferSize)*channels, periodSize);
        else error(sampleBits);
        assert(readSize<=periodSize);
        control->swPointer += readSize;
        if(readSize<periodSize) { stop(); return; }
    }
    if(status->state == Prepared) io<START>();
}
#endif
