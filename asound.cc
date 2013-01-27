#include "asound.h"
#include "string.h"

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
    hparams.interval(Rate) = rate;
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
        if(sampleBits==32) readSize=read32(((int32*)buffer)+(control->swPointer%bufferSize)*channels, periodSize);
        assert(readSize<=periodSize);
        control->swPointer += readSize;
        if(readSize<periodSize) { stop(); return; }
    }
    if(status->state == Prepared) io<START>();
}
