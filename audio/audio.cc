#include "audio.h"
#include "string.h"
#include "linux.h"

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
struct Status { int state, pad; ptr hwPointer; long sec,nsec; int suspended_state; };
struct Control { ptr swPointer; long availableMinimum; };

typedef IOWR<'A', 0x10,HWParams> HW_REFINE;
typedef IOWR<'A', 0x11,HWParams> HW_PARAMS;
typedef IOWR<'A', 0x13,SWParams> SW_PARAMS;
typedef IO<'A', 0x40> PREPARE;
typedef IO<'A', 0x42> START;
typedef IO<'A', 0x44> DRAIN;

Device getPlaybackDevice() {
    Folder snd("/dev/snd");
    for(const String& device: snd.list(Devices))
        if(startsWith(device, "pcm"_) && endsWith(device,"D0p"_)) return Device(device, snd, ReadWrite);
    for(const String& device: snd.list(Devices))
        if(startsWith(device, "pcm"_) && endsWith(device,"p"_)) return Device(device, snd, ReadWrite);
    error("No PCM playback device found"); //FIXME: Block and watch folder until connected
}

AudioOutput::AudioOutput(uint sampleBits, uint rate, uint periodSize, Thread& thread)
    : Device(getPlaybackDevice()), Poll(Device::fd,POLLOUT,thread) { //FIXME: list devices
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
    iowr<HW_PARAMS>(hparams);
    this->sampleBits = hparams.interval(SampleBits);
    this->rate = hparams.interval(Rate);
    this->periodSize = hparams.interval(PeriodSize);
    bufferSize = hparams.interval(Periods) * this->periodSize;
    buffer = (void*)((maps[0]=Map(Device::fd, 0, bufferSize * channels * this->sampleBits/8, Map::Prot(Map::Read|Map::Write))).data);
    status = (Status*)((maps[1]=Map(Device::fd, 0x80000000, 0x1000, Map::Read)).data.pointer);
    control = (Control*)((maps[2]=Map(Device::fd, 0x81000000, 0x1000, Map::Prot(Map::Read|Map::Write))).data.pointer);
}

void AudioOutput::start() { if(status->state != Prepared && status->state != Running) { io<PREPARE>(); registerPoll(); } }
void AudioOutput::stop() { if(status->state == Running) io<DRAIN>(); unregisterPoll(); }
void AudioOutput::event() {
    if(status->state == XRun) { log("Underrun"_); io<PREPARE>(); }
    int available = status->hwPointer + bufferSize - control->swPointer;
    if(available>=(int)periodSize) {
        uint readSize;
        if(sampleBits==16) readSize=read16(mref<short2>(((short2*)buffer)+control->swPointer%bufferSize, periodSize));
        else if(sampleBits==32) readSize=read32(mref<int2>(((int2*)buffer)+control->swPointer%bufferSize, periodSize));
        else error(sampleBits);
        assert(readSize<=periodSize);
        control->swPointer += readSize;
        if(readSize<periodSize) { stop(); return; }
    }
    if(status->state == Prepared) io<START>();
}
void AudioOutput::cancel() { if(status->state == Running) { control->swPointer -= periodSize; event(); } }

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
    assert_(hparams.interval(Channels)==2);
    bufferSize = hparams.interval(Periods) * this->periodSize;
    buffer = (void*)((maps[0]=Map(Device::fd, 0, bufferSize * channels * this->sampleBits/8, Map::Read)).data);
    status = (Status*)((maps[1]=Map(Device::fd, 0x80000000, 0x1000, Map::Read)).data.pointer);
    control = (Control*)((maps[2]=Map(Device::fd, 0x81000000, 0x1000, Map::Prot(Map::Read|Map::Write))).data.pointer);
}
void AudioInput::start() { if(status->state != Running) { io<PREPARE>(); registerPoll(); io<START>(); } }
void AudioInput::stop() { if(status->state == Running) io<DRAIN>(); unregisterPoll(); }
void AudioInput::event() {
    if(status->state == XRun) { overruns++; log("Overrun"_,overruns,"/",periods,"~ 1/",(float)periods/overruns); io<PREPARE>(); io<START>(); }
    int available = status->hwPointer + bufferSize - control->swPointer;
    if(available>=(int)periodSize) {
        uint readSize;
        if(sampleBits==16) readSize=write16(((int16*)buffer)+(control->swPointer%bufferSize)*channels, periodSize);
        else if(sampleBits==32) readSize=write32(((int32*)buffer)+(control->swPointer%bufferSize)*channels, periodSize);
        else error(sampleBits);
        assert(readSize<=periodSize);
        control->swPointer += readSize;
        if(readSize<periodSize) { stop(); return; }
        periods++;
    }
}
