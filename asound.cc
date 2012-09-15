#include "asound.h"
#include "linux.h"
#include "debug.h"

enum State { Open, Setup, Prepared, Running, XRun, Draining, Paused };
enum Access { MMapInterleaved=0 };
enum Format { S16_LE=2 };
enum SubFormat { Standard=0 };
enum MMap { StatusOffset = 0x80000000, ControlOffset = 0x81000000 };
enum Masks { Access, Format, SubFormat };
enum Intervals { SampleBits, FrameBits, Channels, Rate, PeriodTime, PeriodSize, PeriodBytes, Periods, BufferTime, BufferSize };
enum Flags { NoResample=1, ExportBuffer=2, NoPeriodWakeUp=4 };

struct Interval {
    uint min, max; uint openmin:1, openmax:1, integer:1, empty:1;
    Interval():min(0),max(-1),openmin(0),openmax(0),integer(0),empty(0){}
    Interval(uint exact):min(exact),max(exact),openmin(0),openmax(0),integer(1),empty(0){}
    operator uint() { assert_(integer); assert_(min==max); return max; }
};
struct Mask {
    int bits[8] = {~0,~0,0,0,0,0,0,0};
    void set(uint bit) { assert_(bit < 256); bits[0] = bits[1] = 0; bits[bit >> 5] |= (1 << (bit & 31)); }
};
struct HWParams {
    uint flags = NoResample;
    Mask masks[3];
    Mask mres[5];
    Interval intervals[12];
    Interval ires[9];
    uint rmask, cmask, info, msbits, rate_num, rate_den;
    long fifo_size;
    byte reserved[64];
    Interval& interval(int i) { assert_(i<12); return intervals[i]; }
    Mask& mask(int i) { assert_(i<3); return masks[i]; }
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

AudioOutput::AudioOutput(function<bool(int16* output, uint size)> read, Thread& thread, bool realtime)
    : Device("/dev/snd/pcmC0D0p"_,ReadWrite), Poll(Device::fd,POLLOUT,thread), read(read) {
    HWParams hparams;
    hparams.mask(Access).set(MMapInterleaved);
    hparams.mask(Format).set(S16_LE);
    hparams.mask(SubFormat).set(Standard);
    hparams.interval(SampleBits) = 16;
    hparams.interval(FrameBits) = 16*channels;
    hparams.interval(Channels) = channels;
    hparams.interval(Rate) = rate;
    if(realtime) hparams.interval(PeriodSize)=512/*~2x resampler latency*/, hparams.interval(Periods).max=2;
    else hparams.interval(PeriodSize).min=8192, hparams.interval(Periods).min=2;
    iowr<HW_PARAMS>(hparams);
    periodSize = hparams.interval(PeriodSize);
    bufferSize = hparams.interval(Periods) * periodSize;
    buffer= (int16*)check( mmap(0, bufferSize * channels * sizeof(int16), PROT_READ|PROT_WRITE, MAP_SHARED, Device::fd, 0) );
    status = (Status*)check( mmap(0, 0x1000, PROT_READ, MAP_SHARED, Device::fd, StatusOffset) );
    control = (Control*)check( mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, Device::fd, ControlOffset) );
    debug(log("period="_+dec((int)periodSize)+" ("_+dec(1000000*periodSize/rate)+"μs), buffer="_+dec(bufferSize)+" ("_+dec(1000000*bufferSize/rate)+"μs)"_);)
}
AudioOutput::~AudioOutput() {
    munmap((void*)status, 0x1000);
    munmap(control, 0x1000);
    munmap(buffer, bufferSize * channels * 2);
}
void AudioOutput::start() { io<PREPARE>(); }
void AudioOutput::stop() { if(status->state == Running) io<DRAIN>(); }
void AudioOutput::event() {
    assert_(revents!=POLLNVAL);
    if(status->state == XRun) { log("XRun"_); io<PREPARE>(); }
    for(;;){
        int available = status->hwPointer + bufferSize - control->swPointer;
        if(!available) break;
        if(!read(buffer+(control->swPointer%bufferSize)*channels, periodSize)) {stop(); return;}
        control->swPointer += available;
        if(status->state == Prepared) { io<START>(); }
    }
}
