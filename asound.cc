#include "asound.h"
#include "linux.h"
#include "debug.h"

enum STATE { OPEN, SETUP, PREPARED, RUNNING, XRUN, DRAINING, PAUSED, SUSPENDED, DISCONNECTED	};
enum ACCESS { MMAP_INTERLEAVED=0 };
enum FORMAT { S16_LE=2 };
enum MMAP { OFFSET_DATA=0, OFFSET_STATUS = 0x80000000, OFFSET_CONTROL = 0x81000000 };
enum HW_PARAM { FIRST_MASK=0, ACCESS=0, FORMAT, SUBFORMAT, LAST_MASK=SUBFORMAT,
       FIRST_INTERVAL=8, SAMPLE_BITS=8, FRAME_BITS, CHANNELS, RATE, PERIOD_TIME, PERIOD_SIZE, PERIOD_BYTES, PERIODS, BUFFER_TIME, BUFFER_SIZE, BUFFER_BYTES, TICK_TIME, LAST_INTERVAL=TICK_TIME,
       NORESAMPLE=1, EXPORT_BUFFER=2, NO_PERIOD_WAKEUP=4 };
enum SYNC_PTR { HWSYNC=1, APPL=2, AVAIL_MIN=4 };

struct snd_interval { uint min, max; uint openmin:1, openmax:1, integer:1, empty:1; };
struct snd_mask { uint bits[8]; };
struct hw_params {
 uint flags;
 snd_mask masks[HW_PARAM::LAST_MASK - HW_PARAM::FIRST_MASK + 1];
 snd_mask mres[5];
 snd_interval intervals[HW_PARAM::LAST_INTERVAL - HW_PARAM::FIRST_INTERVAL + 1];
 snd_interval ires[9];
 uint rmask, cmask, info, msbits, rate_num, rate_den;
 ulong fifo_size;
 byte reserved[64];
};
struct sw_params {
 int tstamp_mode;
 uint period_step, sleep_min;
 ulong avail_min, xfer_align, start_threshold, stop_threshold, silence_threshold, silence_size, boundary;
 byte reserved[64];
};
struct mmap_status {
 int state, pad1;
 ulong hw_ptr;
 timespec tstamp;
 int suspended_state;
};
struct mmap_control { ulong appl_ptr, avail_min; };

#include <asm-generic/ioctl.h>
#define IOCTL_HW_PARAMS _IOWR('A', 0x11, hw_params)
#define IOCTL_SW_PARAMS _IOWR('A', 0x13, sw_params)
#define IOCTL_SYNC_PTR _IOWR('A', 0x23, sync_ptr)
#define IOCTL_PREPARE _IO('A', 0x40)
#define IOCTL_START _IO('A', 0x42)
#define IOCTL_DROP _IO('A', 0x43)
#define IOCTL_DRAIN _IO('A', 0x44)

static int param_is_mask(int p) { return (p >= HW_PARAM::FIRST_MASK) && (p <= HW_PARAM::LAST_MASK); }
static int param_is_interval(int p) { return (p >= HW_PARAM::FIRST_INTERVAL) && (p <= HW_PARAM::LAST_INTERVAL); }
static snd_interval& param_to_interval(hw_params& p, int n) { return p.intervals[n - HW_PARAM::FIRST_INTERVAL]; }
static snd_mask& param_to_mask(hw_params& p, int n) { return p.masks[n - HW_PARAM::FIRST_MASK]; }
static void param_set_mask(hw_params& p, int n, uint bit) {
    assert(bit < 256); assert(param_is_mask(n));
    snd_mask& m = param_to_mask(p, n); m.bits[0] = 0; m.bits[1] = 0; m.bits[bit >> 5] |= (1 << (bit & 31));
}
static void param_set_min(hw_params& p, int n, uint val) {
    assert(param_is_interval(n)); snd_interval& i = param_to_interval(p, n); i.min = val;
}
/*static void param_set_max(hw_params *p, int n, uint val) {
    assert(param_is_interval(n)); snd_interval& i = param_to_interval(p, n); i.max = val;
}*/
static void param_set_int(hw_params& p, int n, uint val) {
    assert(param_is_interval(n)); snd_interval& i = param_to_interval(p, n); i.min = val; i.max = val; i.integer = 1;
}
static uint param_get_int(hw_params& p, int n) {
    assert(param_is_interval(n)); snd_interval& i = param_to_interval(p, n); assert(i.integer); return i.max;
}

static pcm pcm_open(uint channels=2, uint periodSize=1024, uint periodCount=2, uint rate=48000) {
    pcm pcm; clear(pcm);
    pcm.fd = check( open("/dev/snd/pcmC0D0p", O_RDWR, 0) );

    hw_params params;
    clear(params);
    for (int n = HW_PARAM::FIRST_MASK; n <= HW_PARAM::LAST_MASK; n++) {
            snd_mask& m = param_to_mask(params, n);
            m.bits[0] = m.bits[1] = ~0;
    }
    for (int n = HW_PARAM::FIRST_INTERVAL; n <= HW_PARAM::LAST_INTERVAL; n++) {
            snd_interval& i = param_to_interval(params, n);
            i.min = 0; i.max = ~0;
    }
    param_set_mask(params, HW_PARAM::FORMAT, FORMAT::S16_LE);
    param_set_mask(params, HW_PARAM::SUBFORMAT, 0);
    param_set_min(params, HW_PARAM::PERIOD_SIZE, periodSize);
    param_set_int(params, HW_PARAM::SAMPLE_BITS, 16);
    param_set_int(params, HW_PARAM::FRAME_BITS, 16 * channels);
    param_set_int(params, HW_PARAM::CHANNELS, channels);
    param_set_int(params, HW_PARAM::PERIODS, periodCount);
    param_set_int(params, HW_PARAM::RATE, rate);
    param_set_mask(params, HW_PARAM::ACCESS, ACCESS::MMAP_INTERLEAVED);
    //params.flags |= HW_PARAMS_NO_PERIOD_WAKEUP; pcm.noirq_frames_per_msec = pcm.rate / 1000;

    if(ioctl(pcm.fd, IOCTL_HW_PARAMS, &params)) error( "cannot set hw params");

    pcm.period_size = param_get_int(params, HW_PARAM::PERIOD_SIZE);
    pcm.period_count = param_get_int(params, HW_PARAM::PERIODS);
    pcm.buffer_size = pcm.period_count * pcm.period_size;

    pcm.mmap_buffer= (void*)check(mmap(0, pcm.buffer_size * pcm.channels * 2, PROT_READ | PROT_WRITE, MAP_SHARED, pcm.fd, 0));
    if(pcm.mmap_buffer == 0) error("failed to mmap buffer");

    sw_params sparams;
    clear(sparams);
    sparams.period_step = 1;
    sparams.avail_min = 1;
    sparams.start_threshold = pcm.period_count * pcm.period_size / 2;
    sparams.stop_threshold = pcm.period_count * pcm.period_size;
    sparams.xfer_align = pcm.period_size / 2; /* needed for old kernels */
    sparams.silence_size = 0;
    sparams.silence_threshold = 0;
    pcm.boundary = sparams.boundary = pcm.buffer_size;

    while (pcm.boundary * 2 <= __INT_MAX__ - pcm.buffer_size) pcm.boundary *= 2;

    if(ioctl(pcm.fd, IOCTL_SW_PARAMS, &sparams)) error("cannot set sw params");

    pcm.mmap_status = (mmap_status*)mmap(0, 0x1000, PROT_READ, MAP_SHARED, pcm.fd, MMAP::OFFSET_STATUS);
    pcm.mmap_control = (mmap_control*)mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, pcm.fd, MMAP::OFFSET_CONTROL);
    pcm.mmap_control->avail_min = 1;

    return pcm;
}

static int pcm_prepare(pcm& pcm) { return check(ioctl(pcm.fd, IOCTL_PREPARE, 0)); }

static void pcm_start(pcm& pcm) {
    if(ioctl(pcm.fd, IOCTL_PREPARE, 0) < 0) error("cannot prepare channel");
    if(ioctl(pcm.fd, IOCTL_START, 0) < 0) error("cannot start channel");
    pcm.running = 1;
}

static void pcm_hw_munmap_status(pcm& pcm) {
    if(pcm.mmap_status) { munmap(pcm.mmap_status, 0x1000); pcm.mmap_status = 0; }
    if(pcm.mmap_control) { munmap(pcm.mmap_control, 0x1000); pcm.mmap_control = 0; }
}

static int pcm_stop(pcm& pcm) {
    check_(ioctl(pcm.fd, IOCTL_DROP,0));
    pcm.running = 0;
    return 0;
}

static int pcm_close(pcm& pcm) {
    pcm_hw_munmap_status(pcm);
    pcm_stop(pcm);
    munmap(pcm.mmap_buffer, pcm.buffer_size * pcm.channels * 2);

    if(pcm.fd >= 0) close(pcm.fd);
    pcm.running = 0;
    pcm.buffer_size = 0;
    pcm.fd = -1;
    return 0;
}

static int pcm_drain(pcm& pcm) { return check(ioctl(pcm.fd, IOCTL_DRAIN,0)); }

static int pcm_mmap_avail(pcm& pcm) {
    int avail = pcm.mmap_status->hw_ptr + pcm.buffer_size - pcm.mmap_control->appl_ptr;
    if(avail < 0) avail += pcm.boundary;
    else if(avail > (int)pcm.boundary) avail -= pcm.boundary;
    return avail;
}

static int pcm_mmap_begin(pcm& pcm, void **areas, uint *offset, uint *frames) {
    *areas = pcm.mmap_buffer; // return the mmap buffer
    *offset = pcm.mmap_control->appl_ptr % pcm.buffer_size; //and the application offset in frames
    uint avail = pcm_mmap_avail(pcm);
    if(avail > pcm.buffer_size) avail = pcm.buffer_size;
    uint continuous = pcm.buffer_size - *offset;
    uint copy_frames = *frames; // we can only copy frames if the are availabale and continuous
    if(copy_frames > avail) copy_frames = avail;
    if(copy_frames > continuous) copy_frames = continuous;
    *frames = copy_frames;
    return 0;
}

// update the application pointer in userspace and kernel
static int pcm_mmap_commit(pcm& pcm, uint unused offset, uint frames) {
    uint appl_ptr = pcm.mmap_control->appl_ptr;
    appl_ptr += frames;
    if(appl_ptr > pcm.boundary) appl_ptr -= pcm.boundary; //check for boundary wrap
    pcm.mmap_control->appl_ptr = appl_ptr;
    return frames;
}

static int pcm_state(pcm& pcm) { return pcm.mmap_status->state; }

AudioOutput::AudioOutput(function<void(int16* output, uint size)> read, bool unused realtime) : read(read) {
    pcm=pcm_open();
}
AudioOutput::~AudioOutput(){ pcm_close(pcm); }
void AudioOutput::start() { if(running) return; registerPoll(__(pcm.fd,POLLOUT)); running=true; }
void AudioOutput::stop() { if(!running) return; unregisterPoll(); pcm_drain(pcm); running=false; }
void AudioOutput::event(const pollfd& p) {
    if(!(p.revents & POLLOUT)) { warn(p.revents); return; }
    if( pcm_state(pcm) == STATE::XRUN ) { warn("xrun"_); pcm_prepare(pcm); }
    uint frames = pcm_mmap_avail(pcm);
    assert(frames >= period);
    void* buffer; uint offset;
    frames=period;
    pcm_mmap_begin(pcm, &buffer, &offset, &frames);
    assert(frames == period);
    int16* output = (int16*)buffer;
    read(output,(int)period);
    pcm_mmap_commit(pcm, offset, frames);
    if(pcm_state(pcm) == STATE::PREPARED && pcm_mmap_avail(pcm)==0) pcm_start(pcm);
    return;
}
