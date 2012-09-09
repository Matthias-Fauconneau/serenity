#include "asound.h"
#include "linux.h"
#include "debug.h"

enum State { Open, Setup, Prepared, Running, XRun, Draining, Paused, Suspended, Disconnected };
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
    operator uint() { assert(integer); assert(min==max); return max; }
};
struct Mask {
    int bits[8] = {~0,~0,0,0,0,0,0,0};
    void set(uint bit) { assert(bit < 256); bits[0] = bits[1] = 0; bits[bit >> 5] |= (1 << (bit & 31)); }
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
    Interval& interval(int i) { assert(i<12); return intervals[i]; }
    Mask& mask(int i) { assert(i<3); return masks[i]; }
};
struct SWParams {
 int tstamp_mode=0;
 uint period_step=1, sleep_min=0;
 long avail_min=0, xfer_align=0, start_threshold=0, stop_threshold=0, silence_threshold=0, silence_size=0, boundary=0;
 byte reserved[64];
};
struct Status { int state, pad; ptr hwPointer; timespec tstamp; int suspended_state; };
struct Control { ptr swPointer; long availableMinimum; };

enum IOCTL {
    HW_PARAMS = IOWR<HWParams>('A', 0x11),
    SW_PARAMS =IOWR<SWParams>('A', 0x13),
    PREPARE = IO('A', 0x40),
    START = IO('A', 0x42),
    DRAIN = IO('A', 0x44)
};

void AudioOutput::start(bool realtime) {
    if(fd) return;
    fd = open("/dev/snd/pcmC0D0p", O_RDWR|O_NONBLOCK, 0);
    if(fd<0) { log(errno[-fd]); fd=0; return; }

    HWParams hparams;
    hparams.mask(Access).set(MMapInterleaved);
    hparams.mask(Format).set(S16_LE);
    hparams.mask(SubFormat).set(Standard);
    hparams.interval(SampleBits) = 16;
    hparams.interval(FrameBits) = 16*channels;
    hparams.interval(Channels) = channels;
    hparams.interval(Rate) = rate;
    if(realtime) hparams.interval(PeriodSize)=512, hparams.interval(Periods).max=2;
    else hparams.interval(PeriodSize).min=1024, hparams.interval(Periods).min=2;
    ioctl(HW_PARAMS, &hparams);
    periodSize = hparams.interval(PeriodSize);
    bufferSize = hparams.interval(Periods) * periodSize;
    debug(log("period="_+dec((int)periodSize)+" ("_+dec(1000*periodSize/rate)+"ms), buffer="_+dec(bufferSize)+" ("_+dec(1000*bufferSize/rate)+"ms)"_);)
    buffer= (int16*)mmap(0, bufferSize * channels * sizeof(int16), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(buffer);

    SWParams sparams;
    sparams.avail_min = hparams.interval(PeriodSize);
    sparams.stop_threshold = sparams.boundary = bufferSize;
    ioctl(SW_PARAMS, &sparams);

    status = (Status*)check( mmap(0, 0x1000, PROT_READ, MAP_SHARED, fd, StatusOffset) );
    control = (Control*)check( mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, ControlOffset) );
    ioctl(PREPARE, 0);
    registerPoll(fd, POLLOUT|POLLERR|POLLNVAL);
}
void AudioOutput::stop() {
    if(!fd) return; unregisterPoll();
    if(status->state == Running) ioctl(DRAIN,0);
    munmap((void*)status, 0x1000); status=0;
    munmap(control, 0x1000); control=0;
    munmap(buffer, bufferSize * channels * 2); buffer=0; bufferSize=0;
    close(fd); fd=0;
}
void AudioOutput::event() {
    assert(revents!=POLLNVAL);
    if(status->state == XRun) { log("XRun"_); ioctl(PREPARE, 0); }
    for(;;){
        int available = status->hwPointer + bufferSize - control->swPointer;
        if(!available) break; assert(available>=int(periodSize));
        uint offset = control->swPointer % bufferSize;  assert(bufferSize-offset>=periodSize);
        if(!read(buffer+offset*channels, periodSize)) {stop(); return;}
        control->swPointer += periodSize;
        if(status->state == Prepared) { ioctl(START, 0); }
    }
}
