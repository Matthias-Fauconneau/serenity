#include "asound.h"
#include "linux.h"
#include "debug.h"

enum State { Open, Setup, Prepared, Running, XRun, Draining, Paused, Suspended, Disconnected };
enum Access { MMapInterleaved=0 };
enum Format { S16_LE=2 };
enum SubFormat { Standard=0 };
enum MMap { StatusOffset = 0x80000, ControlOffset = 0x81000 };
enum Masks { Access, Format, SubFormat };
enum Intervals { SampleBits, FrameBits, Channels, Rate, PeriodTime, PeriodSize, PeriodBytes, Periods, BufferTime, BufferSize };
enum Flags { NoResample=1, ExportBuffer=2, NoPeriodWakeUp=4 };

struct interval {
    uint min, max; uint openmin:1, openmax:1, integer:1, empty:1;
    interval():min(0),max(-1),openmin(0),openmax(0),integer(0),empty(0){}
    interval(uint exact):min(exact),max(exact),openmin(0),openmax(0),integer(1),empty(0){}
    operator uint() { assert(integer); assert(min==max); return max; }
};
struct mask {
    int bits[8] = {~0,~0,0,0,0,0,0,0};
    void set(uint bit) { assert(bit < 256); bits[0] = bits[1] = 0; bits[bit >> 5] |= (1 << (bit & 31)); }
};
struct HWParams {
    uint flags = NoResample; //|NoPeriodWakeUp
    mask masks[3];
    mask mres[5];
    interval intervals[12];
    interval ires[9];
    uint rmask, cmask, info, msbits, rate_num, rate_den;
    ulong fifo_size;
    byte reserved[64];
    interval& interval(int i) { assert(i<12); return intervals[i]; }
    mask& mask(int i) { assert(i<3); return masks[i]; }
};
struct SWParams {
 int tstamp_mode=0;
 uint period_step=1, sleep_min=0;
 ulong avail_min=0, xfer_align=0, start_threshold=0, stop_threshold=0, silence_threshold=0, silence_size=0, boundary=0;
 byte reserved[64];
};
struct Status { int state, pad; ulong hwPointer; timespec tstamp; int suspended_state; };
struct Control { ulong swPointer, availableMinimum; };

#define IO(major,minor) major<<8 | minor
#define IOWR(major,minor,type) 3<<30 | sizeof(type)<<16 | major<<8 | minor
#define IOCTL_HW_PARAMS IOWR('A', 0x11, HWParams)
#define IOCTL_SW_PARAMS IOWR('A', 0x13, SWParams)
#define IOCTL_PREPARE IO('A', 0x40)
#define IOCTL_START IO('A', 0x42)
#define IOCTL_DRAIN IO('A', 0x44)

void AudioOutput::start(bool realtime) {
    if(fd) return;
    fd = open("/dev/snd/pcmC0D0p", O_RDWR|O_NONBLOCK, 0);
    if(fd<0) { warn("Busy audio output"); fd=0; return; }

    HWParams hparams;
    hparams.mask(Access).set(MMapInterleaved);
    hparams.mask(Format).set(S16_LE);
    hparams.mask(SubFormat).set(Standard);
    hparams.interval(SampleBits) = 16;
    hparams.interval(FrameBits) = 16 * channels;
    hparams.interval(Channels) = channels;
    hparams.interval(Rate) = rate;
    hparams.interval(PeriodSize).min= periodSize;
    if(realtime) hparams.interval(Periods).max = 2;
    else hparams.interval(Periods).min = 2;
    check_(ioctl(fd, IOCTL_HW_PARAMS, &hparams));
    periodSize = hparams.interval(PeriodSize);
    periodCount = hparams.interval(Periods);
    bufferSize = periodCount * periodSize;
    buffer= (int16*)mmap(0, bufferSize * channels * sizeof(int16), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(buffer);

    SWParams sparams;
    sparams.avail_min = periodSize;
    sparams.stop_threshold = sparams.boundary = bufferSize;
    check_(ioctl(fd, IOCTL_SW_PARAMS, &sparams));

    status = (Status*)mmap(0, 0x1000, PROT_READ, MAP_SHARED, fd, StatusOffset);
    control = (Control*)mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, ControlOffset);
    registerPoll(POLLOUT|POLLERR|POLLNVAL);
    ioctl(fd, IOCTL_PREPARE, 0);
}
void AudioOutput::stop() {
    if(!fd) return; unregisterPoll();
    if(status->state == Running) ioctl(fd, IOCTL_DRAIN,0);
    munmap(status, 0x1000); status=0;
    munmap(control, 0x1000); control=0;
    munmap(buffer, bufferSize * channels * 2); buffer=0; bufferSize=0;
    close(fd); fd=0;
}
void AudioOutput::event(const pollfd&) {
    if(status->state == XRun) { warn("XRun"_); check_(ioctl(fd, IOCTL_PREPARE, 0)); }
    int available = status->hwPointer + bufferSize - control->swPointer;
    if(!available){/*log(status->state,bufferSize,"=",periodCount,"x",periodSize,"hw",status->hwPointer,"sw",control->swPointer);*/return;}//FIXME
    uint offset = control->swPointer % bufferSize;
    uint frames = min(min((uint)available,bufferSize),bufferSize-offset);
    read(buffer+offset*channels, frames);
    control->swPointer += frames;
    if(status->state == Prepared) { check_(ioctl(fd, IOCTL_START, 0)); }
}
