#include "audio.h"
#include "string.h"
#include "data.h"

#if ASOUND
#include <sys/types.h>
#include <alsa/global.h>
struct snd_input_t;
struct snd_output_t;
#include <alsa/conf.h>
#include <alsa/pcm.h> //asound
#include <alsa/timer.h>
#include <alsa/control.h>

AudioOutput::AudioOutput(function<uint(const mref<short2>& output)> read, Thread& thread)
    : Poll(0, POLLOUT, thread), read16(read) {
    check( snd_pcm_open(&pcm,"default",SND_PCM_STREAM_PLAYBACK,SND_PCM_NO_SOFTVOL|SND_PCM_NO_AUTO_RESAMPLE) );
}
void AudioOutput::start(uint rate, uint periodSize, uint sampleBits) {
    if(running) return;
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
    check( snd_pcm_hw_params_get_rate(hw, &rate, 0) ); this->rate=rate;
    check( snd_pcm_hw_params_set_period_size(pcm, hw, periodSize, 0), periodSize);
    snd_pcm_uframes_t period_size; check( snd_pcm_hw_params_get_period_size(hw, &period_size, 0) ); this->periodSize=period_size;
    uint periods=0; check( snd_pcm_hw_params_set_periods_first(pcm, hw, &periods, 0) ); bufferSize = periods * this->periodSize;
    check( snd_pcm_hw_params(pcm, hw) );
    byte alloca_sw[snd_pcm_sw_params_sizeof()];
    snd_pcm_sw_params_t *sw=(snd_pcm_sw_params_t*)alloca_sw;
    snd_pcm_sw_params_current(pcm, sw);
    snd_pcm_sw_params_set_avail_min(pcm, sw, this->periodSize);
    snd_pcm_sw_params(pcm,sw);
    snd_pcm_poll_descriptors(pcm,this,1); registerPoll(); running=true;
}
void AudioOutput::stop() { if(!running) return; unregisterPoll(); snd_pcm_drain(pcm); running=false; }
void AudioOutput::event() {
    unsigned short revents;
    snd_pcm_poll_descriptors_revents(pcm, this, 1, &revents);
    if(!(revents & POLLOUT)) return;
    if( snd_pcm_state(pcm) == SND_PCM_STATE_XRUN ) { log("Underrun"_); snd_pcm_prepare(pcm); }
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
        if(sampleBits==16) frames=read16(mcast<short2>(audio));
        else error(sampleBits);
        snd_pcm_writei(pcm, audio.data, frames);
#endif
        if(snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED) snd_pcm_start(pcm);
    }
}

#else
enum State { Open, Setup, Prepared, Running, XRun, Draining, Paused };
enum Access { MMapInterleaved=0 };
enum Format { S16_LE=2, S32_LE=10 };
enum SubFormat { Standard=0 };
enum Masks { Access, Format, SubFormat };
enum Intervals { SampleBits, FrameBits, Channels, Rate, PeriodTime, PeriodSize, PeriodBytes, Periods, BufferTime, BufferSize };
enum PCMFlags { NoResample=1, ExportBuffer=2, NoPeriodWakeUp=4 };
struct Interval {
    uint min, max; uint openmin:1, openmax:1, integer:1, empty:1, pad:28;
    Interval():min(0),max(-1),openmin(0),openmax(0),integer(0),empty(0),pad(0){}
    /// \note Negative values means are minimum up to driver capabilities
    Interval(int value):min(value),max(value?:-1),openmin(0),openmax(0),integer(1),empty(0),pad(0){}
    operator uint() { assert(integer); assert(min==max); return max; }
};
struct Mask {
    int bits[8] = {0,0,0,0,0,0,0,0};
    bool get(uint bit) { assert(bit < 256); return bits[bit >> 5] & (1 << (bit & 31)); }
    Mask& set(uint bit) { assert(bit < 256); bits[bit >> 5] |= (1 << (bit & 31)); return *this; }
    Mask& clear(uint bit) { assert(bit < 256); bits[bit >> 5] &= ~(1 << (bit & 31)); return *this; }
};
struct HWParams {
    uint flags = NoResample;
    Mask masks[3];
    Mask mres[5];
    Interval intervals[12];
    Interval ires[9];
    uint rmask=~0, cmask=0, info=0, msbits=0, rate_num=0, rate_den=0;
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

typedef IOWR<'A', 0x10,HWParams> HW_REFINE;
typedef IOWR<'A', 0x11,HWParams> HW_PARAMS;
typedef IOWR<'A', 0x13,SWParams> SW_PARAMS;
typedef IO<'A', 0x40> PREPARE;
typedef IO<'A', 0x41> RESET;
typedef IO<'A', 0x42> START;
typedef IO<'A', 0x43> DROP;
typedef IO<'A', 0x44> DRAIN;

#if !MMAP
// No direct memory access support on OMAP
enum { HWSYNC=1<<0, APPL=1<<1, AVAIL_MIN=1<<2 };
typedef IOWR<'A', 0x23,SyncPtr> SYNC_PTR;
#endif

/// Playback

Device getPlaybackDevice() {
    Folder snd("/dev/snd");
    for(const String& device: snd.list(Devices))
        if(startsWith(device, "pcm"_) && endsWith(device,"D0p"_)) return Device(device, snd, ReadWrite);
    for(const String& device: snd.list(Devices))
        if(startsWith(device, "pcm"_) && endsWith(device,"p"_)) return Device(device, snd, ReadWrite);
    error("No PCM playback device found"); //FIXME: Block and watch folder until connected
}

AudioOutput::AudioOutput(function<uint(const mref<short2>& output)> read, Thread& thread)
    : Device(getPlaybackDevice()), Poll(0, POLLOUT, thread), read16(read) {
}

void AudioOutput::start(uint rate, uint periodSize, uint sampleBits) {
    if(this->rate!=rate || this->periodSize!=periodSize || this->sampleBits!=sampleBits) {
        HWParams hparams;
        hparams.mask(Access).set(MMapInterleaved);
        if(sampleBits) hparams.mask(Format).set(sampleBits==16?S16_LE:S32_LE);
        else hparams.mask(Format).set(S16_LE).set(S32_LE);
        hparams.mask(SubFormat).set(Standard);
        hparams.interval(SampleBits) = sampleBits;
        hparams.interval(FrameBits) = sampleBits*channels;
        hparams.interval(Channels) = channels;
        hparams.interval(Rate) = rate; assert(rate);
        hparams.interval(Periods) = 2;
        hparams.interval(PeriodSize) = periodSize;
        if(!sampleBits || !rate || !periodSize) {
            iowr<HW_REFINE>(hparams);
            if(!sampleBits) {
                if(hparams.mask(Format).get(S32_LE)) {
                    hparams.mask(Format).clear(S16_LE);
                    hparams.interval(SampleBits) = 32;
                    hparams.interval(FrameBits) = 32*channels;
                } else {
                    hparams.interval(SampleBits) = 16;
                    hparams.interval(FrameBits) = 16*channels;
                }
                hparams.rmask=~0;
                iowr<HW_REFINE>(hparams);
            }
            hparams.interval(Rate) = hparams.interval(Rate).max; // Selects maximum rate
            hparams.interval(PeriodSize) = hparams.interval(PeriodSize).max; // Selects maximum latency
        }
        if(status && status->state > Prepared) io<DRAIN>();
        maps[0].unmap(); maps[1].unmap(); maps[2].unmap(); // Releases any memory mappings
        iowr<HW_PARAMS>(hparams);
        this->sampleBits = hparams.interval(SampleBits);
        this->rate = hparams.interval(Rate);
        this->periodSize = hparams.interval(PeriodSize);
        bufferSize = hparams.interval(Periods) * this->periodSize;
        buffer = (void*)((maps[0]=Map(Device::fd, 0, bufferSize * channels * this->sampleBits/8, Map::Prot(Map::Read|Map::Write))).data);
#if MMAP
        status = (Status*)((maps[1]=Map(Device::fd, 0x80000000, 0x1000, Map::Read)).data.pointer);
        control = (Control*)((maps[2]=Map(Device::fd, 0x81000000, 0x1000, Map::Prot(Map::Read|Map::Write))).data.pointer);
#else
        status = &syncPtr.status;
        control = &syncPtr.control;
#endif
        control->availableMinimum = periodSize; // Minimum available space to trigger POLLOUT
    }
    if(!thread.contains(this)) { Poll::fd = Device::fd; registerPoll(); }
#if !MMAP
    syncPtr.flags=APPL; iowr<SYNC_PTR>(syncPtr);
#endif
    if(status->state < Prepared) io<PREPARE>();
    event();
    if(status->state < Running) io<START>();
}
void AudioOutput::stop(){
     io<DRAIN>();
     unregisterPoll();
}

void AudioOutput::event() {
#if !MMAP
    syncPtr.flags=APPL; iowr<SYNC_PTR>(syncPtr);
#endif
    if(status->state == XRun) {
        io<PREPARE>();
#if !MMAP
        syncPtr.flags=APPL; iowr<SYNC_PTR>(syncPtr);
#endif
    }
    int available = status->hwPointer + bufferSize - control->swPointer;
    if(available>=(int)periodSize) {
        uint readSize;
        if(sampleBits==16) readSize=read16(mref<short2>(((short2*)buffer)+control->swPointer%bufferSize, periodSize));
        //else if(sampleBits==32) readSize=read32(mref<int2>(((int2*)buffer)+control->swPointer%bufferSize, periodSize));
        else error("Unsupported sample size", sampleBits);
        assert(readSize<=periodSize);
        control->swPointer += readSize;
#if !MMAP
        syncPtr.flags = 0; iowr<SYNC_PTR>(syncPtr);
#endif
        if(readSize<periodSize) return;
    }
}

/// Capture

Device getCaptureDevice() {
    Folder snd("/dev/snd");
    for(const String& device: snd.list(Devices))
        if(startsWith(device, "pcm"_) && endsWith(device,"D0c"_)) return Device(device, snd, ReadWrite);
    for(const String& device: snd.list(Devices))
        if(startsWith(device, "pcm"_) && endsWith(device,"c"_)) return Device(device, snd, ReadWrite);
    error("No PCM playback device found"); //FIXME: Block and watch folder until connected
}

AudioInput::AudioInput(uint sampleBits, uint rate, uint periodSize, Thread& thread) : Device(getCaptureDevice()), Poll(Device::fd,POLLIN,thread) {
    HWParams hparams;
    hparams.mask(Access).set(MMapInterleaved);
    if(sampleBits) hparams.mask(Format).set(sampleBits==16?S16_LE:S32_LE);
    else hparams.mask(Format).set(S16_LE).set(S32_LE);
    hparams.mask(SubFormat).set(Standard);
    hparams.interval(SampleBits) = sampleBits;
    hparams.interval(FrameBits) = sampleBits*channels;
    hparams.interval(Channels) = channels;
    hparams.interval(Rate) = rate; assert(rate);
    hparams.interval(Periods) = 2;
    hparams.interval(PeriodSize).max = periodSize?:-1;
    iowr<HW_REFINE>(hparams);
    if(!sampleBits) {
        if(hparams.mask(Format).get(S32_LE)) {
            hparams.mask(Format).clear(S16_LE);
            hparams.interval(SampleBits) = 32;
            hparams.interval(FrameBits) = 32*channels;
        } else {
            hparams.interval(SampleBits) = 16;
            hparams.interval(FrameBits) = 16*channels;
        }
        hparams.rmask=~0;
        iowr<HW_REFINE>(hparams);
    }
    if(!rate) hparams.interval(Rate) = hparams.interval(Rate).max; // Selects maximum rate
    hparams.interval(PeriodSize) = hparams.interval(PeriodSize).max; // Selects maximum latency
    iowr<HW_PARAMS>(hparams);
    this->sampleBits = hparams.interval(SampleBits);
    this->rate = hparams.interval(Rate);
    this->periodSize = hparams.interval(PeriodSize);
    assert(hparams.interval(Channels)==2);
    bufferSize = hparams.interval(Periods) * this->periodSize;
    buffer = (void*)((maps[0]=Map(Device::fd, 0, bufferSize * channels * this->sampleBits/8, Map::Read)).data);
#if MMAP
    status = (Status*)((maps[1]=Map(Device::fd, 0x80000000, 0x1000, Map::Read)).data.pointer);
    control = (Control*)((maps[2]=Map(Device::fd, 0x81000000, 0x1000, Map::Prot(Map::Read|Map::Write))).data.pointer);
#else
    status = &syncPtr.status;
    control = &syncPtr.control;
    control->availableMinimum = periodSize; // Minimum available space to trigger POLLIN
    io<PREPARE>();
    io<START>();
#endif
}
AudioInput::~AudioInput(){
     io<DRAIN>();
}

void AudioInput::event() {
#if !MMAP
    syncPtr.flags=APPL; iowr<SYNC_PTR>(syncPtr);
#endif
    if(status->state == XRun) {
        overruns++;
        log("Overrun"_,overruns,"/",periods,"~ 1/",(float)periods/overruns);
        io<PREPARE>();
        io<START>();
#if !MMAP
        syncPtr.flags=APPL; iowr<SYNC_PTR>(syncPtr);
#endif
    }
    int available = status->hwPointer + bufferSize - control->swPointer;
    if(available>=(int)periodSize) {
        uint readSize;
        if(sampleBits==32) readSize=write32(ref<int2>(((int2*)buffer)+(control->swPointer%bufferSize), periodSize));
        else error(sampleBits);
        assert(readSize<=periodSize);
        control->swPointer += readSize;
#if !MMAP
        syncPtr.flags = 0; iowr<SYNC_PTR>(syncPtr);
#endif
        assert_(readSize==periodSize);
        periods++;
    }
}
#endif
