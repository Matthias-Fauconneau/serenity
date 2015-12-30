#include "asound.h"
#include "string.h"
#include "data.h"
#include "thread.h"
#include "file.h"
#include "string.h"

struct CardInfo {
 int card;                       /* card number */
 int pad;                        /* reserved for future (was type) */
 char id[16];           /* ID of card (user selectable) */
 char driver[16];       /* Driver name */
 char name[32];         /* Short name of soundcard */
 char longname[80];     /* name + info text about soundcard */
 char reserved_[16];    /* reserved for future (was ID of mixer) */
 char mixername[80];    /* visual mixer identification */
 char components[128];  /* card components / fine identification, delimited with one space (AC97 etc..) */
};
typedef IOR<'U', 0x01, CardInfo> CARD_INFO;

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
 int tstamp_mode=1;
 uint period_step=1, sleep_min=0;
 long avail_min=0, xfer_align=0, start_threshold=0, stop_threshold=0, silence_threshold=0, silence_size=0, boundary=0;
 byte reserved[64];
};

typedef IOWR<'A', 0x10,HWParams> HW_REFINE;
typedef IOWR<'A', 0x11,HWParams> HW_PARAMS;
typedef IO<'A', 0x12> HW_FREE;
typedef IOWR<'A', 0x13,SWParams> SW_PARAMS;
typedef IO<'A', 0x40> PREPARE;
typedef IO<'A', 0x42> START;
typedef IO<'A', 0x43> DROP;
typedef IO<'A', 0x44> DRAIN;

/// Playback

bool playbackDeviceAvailable() {
 Folder snd("/dev/snd");
 for(const String& device: snd.list(Devices))
  if(startsWith(device, "pcm") && endsWith(device,"D0p")) return writableFile(device, snd);
 for(const String& device: snd.list(Devices))
  if(startsWith(device, "pcm") && endsWith(device,"p")) return writableFile(device, snd);
 error("No PCM playback device found"); //FIXME: Block and watch folder until connected
}

Device getPlaybackDevice() {
 Folder snd("/dev/snd");
 for(const String& device: snd.list(Devices))
  if(startsWith(device, "pcm") && endsWith(device,"D0p")) return Device(device, snd, Flags(ReadWrite/*|NonBlocking*/));
 for(const String& device: snd.list(Devices))
  if(startsWith(device, "pcm") && endsWith(device,"p")) return Device(device, snd, Flags(ReadWrite/*|NonBlocking*/));
 error("No PCM playback device found"); //FIXME: Block and watch folder until connected
}

AudioOutput::AudioOutput(Thread& thread) : Poll(0, POLLOUT, thread) {}
AudioOutput::AudioOutput(decltype(read16) read, Thread& thread) : Poll(0, POLLOUT, thread), read16(read) {}
AudioOutput::AudioOutput(decltype(read32) read, Thread& thread) : Poll(0, POLLOUT, thread), read32(read) {}

bool AudioOutput::start(uint rate, uint periodSize, uint sampleBits, uint channels) {
 if(!Device::fd) { Device::fd = move(getPlaybackDevice().fd); Poll::fd = Device::fd; }
 if(!Device::fd) { log("AudioOutput::start failed"); return false; }
 //{CardInfo info = ior<CARD_INFO>(); log(info.card, info.id, info.driver, info.name, info.longname, info.mixername, info.components); }
 if(!status || status->state < Setup || this->rate!=rate || this->periodSize!=periodSize || this->sampleBits!=sampleBits) {
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
  this->channels = hparams.interval(Channels);
  this->rate = hparams.interval(Rate);
  this->periodSize = hparams.interval(PeriodSize);
  bufferSize = hparams.interval(Periods) * this->periodSize;
  log(sampleBits, channels, rate, periodSize, bufferSize);
  buffer = (void*)((maps[0]=Map(Device::fd, 0, bufferSize * channels * this->sampleBits/8, Map::Prot(Map::Read|Map::Write))).data);
  status = (Status*)(maps[1]=Map(Device::fd, 0x80000000, 0x1000, Map::Read)).data;
  control = (Control*)(maps[2]=Map(Device::fd, 0x81000000, 0x1000, Map::Prot(Map::Read|Map::Write))).data;
  control->availableMinimum = periodSize; // Minimum available space to trigger POLLOUT
 }
 registerPoll();
 if(status->state < Prepared) {
  assert_(status->state == Setup, status->state);
  io<PREPARE>();
 }
 //event();
 return true;
}

void AudioOutput::stop() {
 assert_(status);
 if(status->state == Running) io<DRAIN>(int(LinuxError::Again)); // FIXME: set fd to blocking first
 else if(status->state != Prepared) log("Could not drain", status->state);
 unregisterPoll();
 {int state = status->state;
  buffer=0, maps[0].unmap(); status=0, maps[1].unmap(); control=0, maps[2].unmap(); // Release maps
  if(state < Running || state==XRun) io<HW_FREE>(); // Releases hardware
  else log("Could not free", state);}
 sampleBits=0, rate=0, periodSize=0, bufferSize=0;
 close(); Poll::fd=0; // Closes file descriptor
}

void AudioOutput::event() {
 if(status->state < Prepared) {
  assert_(status->state == Setup, status->state);
  io<PREPARE>();
 }
 if(status->state == XRun) { io<PREPARE>(); underruns++; log("Underrun", underruns); }
 int available = status->hwPointer + bufferSize - control->swPointer;
 if(available>=(int)periodSize) {
  uint readSize;
  /**/  if(sampleBits==16) readSize=read16(mref<short2>(((short2*)buffer)+control->swPointer%bufferSize, periodSize));
  else if(sampleBits==32) {
   /**/   /*if(channels==1) readSize=read32m(mref<int>(((int*)buffer)+control->swPointer%bufferSize, periodSize));
   else*/ if(channels==2) readSize=read32(mref<int2>(((int2*)buffer)+control->swPointer%bufferSize, periodSize));
   else error(channels);
  } else error("Unsupported sample size", sampleBits);
  assert(readSize<=periodSize);
  if(!control) return; // Was closed from read callback
  control->swPointer += readSize;
  if(readSize < periodSize) return;
 }
 if(status->state < Running) {
  uint ready = control->swPointer - status->hwPointer;
  assert_(status->state == Prepared && ready >= periodSize, status->state, ready, available);
  io<START>();
 }
}

/// Capture

Device getCaptureDevice() {
 Folder snd("/dev/snd");
 for(const String& device: snd.list(Devices))
  if(startsWith(device, "pcm") && endsWith(device,"D0c")) return Device(device, snd, ReadWrite);
 for(const String& device: snd.list(Devices))
  if(startsWith(device, "pcm") && endsWith(device,"c")) return Device(device, snd, ReadWrite);
 error("No PCM playback device found"); //FIXME: Block and watch folder until connected
}

AudioInput::AudioInput(Thread& thread) : Poll(Device::fd,POLLIN,thread) {}
void AudioInput::setup(uint channels, uint rate, uint periodSize) {
 Locker locker(lock);
 if(!Device::fd) { Device::fd = move(getCaptureDevice().fd); Poll::fd = Device::fd; }
 if(!status || status->state < Setup || this->rate!=rate || this->periodSize!=periodSize || this->sampleBits!=sampleBits) {
  HWParams hparams;
  hparams.mask(Access).set(MMapInterleaved);
  if(sampleBits) hparams.mask(Format).set(sampleBits==16?S16_LE:S32_LE);
  else hparams.mask(Format).set(S16_LE).set(S32_LE);
  hparams.interval(SampleBits) = sampleBits;
  hparams.interval(FrameBits) = sampleBits*channels;
  hparams.mask(SubFormat).set(Standard);
  hparams.interval(Channels).max = channels;
  hparams.interval(Rate) = rate; assert(rate);
  hparams.interval(Periods) = 2;
  hparams.interval(PeriodSize) = periodSize?:-1;
  iowr<HW_REFINE>(hparams);
  if(!sampleBits) {
   channels = hparams.interval(Channels);
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
  assert_(hparams.flags == NoResample);
  assert_(this->sampleBits == hparams.interval(SampleBits));
  this->channels= hparams.interval(Channels);
  assert_(this->channels == channels);
  this->rate = hparams.interval(Rate);
  assert_(this->rate == rate);
  this->periodSize = hparams.interval(PeriodSize);
  assert_(this->periodSize == periodSize);
  bufferSize = hparams.interval(Periods) * this->periodSize;
  buffer = (void*)((maps[0]=Map(Device::fd, 0, bufferSize * channels * this->sampleBits/8, Map::Read)).data);
  status = (Status*)(maps[1]=Map(Device::fd, 0x80000000, 0x1000, Map::Read)).data;
  control = (Control*)(maps[2]=Map(Device::fd, 0x81000000, 0x1000, Map::Prot(Map::Read|Map::Write))).data;
  control->availableMinimum = periodSize; // Minimum available space to trigger POLLIN
 }
 registerPoll();
 if(status->state < Prepared) io<PREPARE>();
 time = 0;
}
void AudioInput::start() {
 Locker locker(lock);
 assert_(status);
 io<START>();
}
void AudioInput::stop() {
 Locker locker(lock);
 assert_(status);
 if(status->state == XRun) io<PREPARE>(); // FIXME: Necessary to prevent BADFD error on HW_FREE
 io<DROP>();
 unregisterPoll();
 {int state = status->state;
  buffer=0, maps[0].unmap(); status=0, maps[1].unmap(); control=0, maps[2].unmap(); // Release maps
  if(state < Running) io<HW_FREE>(); // Releases hardware
  else log("HW_FREE", state);
 }
 rate=0, periodSize=0, bufferSize=0;
 close(); Poll::fd=0; // Closes file descriptor
 time = 0;
}

void AudioInput::event() {
 Locker locker(lock);
 if(!status) return; // Closed by a concurrent thread
 if(status->state == XRun) {
  overruns++;
  //log("Overrun",overruns,"/",periods,"~ 1/",(float)periods/overruns);
  io<PREPARE>();
  io<START>();
 }
 int available = status->hwPointer + bufferSize - control->swPointer;
 if(available>=(int)periodSize) {
  uint readSize;
  if(sampleBits==32) readSize=write32(ref<int32>(((int*)buffer)+channels*(control->swPointer%bufferSize), periodSize*channels));
  else error(sampleBits);
  assert_(readSize==periodSize, readSize, periodSize);
  control->swPointer += readSize;
  time += readSize;
  periods += 1;
 }
}

/// Control

struct ID { uint numid, iface, device, subdevice; char name[44]; uint index; };
struct List { uint offset, capacity, used, count; ID* pids; byte reserved[50]; };
struct Info { ID id; uint type, access, count, owner; long min, max, step; byte reserved[128-sizeof(long[3])+64]; };
struct Value { ID id; uint indirect; long values[128]; byte reserved[128]; };

typedef IOWR<'U', 0x10, List> ELEM_LIST;
typedef IOWR<'U', 0x11, Info> ELEM_INFO;
typedef IOWR<'U', 0x12, Value> ELEM_READ;
typedef IOWR<'U', 0x13, Value> ELEM_WRITE;

Device getControlDevice() {
 Folder snd("/dev/snd");
 for(const String& device: snd.list(Devices)) {
  if(!startsWith(device, "controlC")) continue;
  Device control(device, snd, ReadWrite);
  /*List list = {};
        control.iowr<ELEM_LIST>(list);
        if(!list.count) { log("No control available"); continue; }
        log(device);*/
  return control;
 }
 error("No control device found");
}

AudioControl::AudioControl(string name) : Device(getControlDevice()) {
 List list = {};
 iowr<ELEM_LIST>(list);
 if(!list.count) { log("No control available"); return; }
 ID ids[list.count];
 list.capacity = list.count;
 list.pids = ids;
 iowr<ELEM_LIST>(list);
 buffer<string> names (list.count);
 for(int i: range(list.count)) {
  Info info;
  info.id.numid = ids[i].numid;
  iowr<ELEM_INFO>(info);
  names[i] = string(info.id.name);
  if(startsWith(string(info.id.name),name)) { id=info.id.numid; min=info.min, max=info.max; return; }
 }
 error("No such control"_, name, "in", names, list.count);
}

AudioControl::operator long() {
 Value value; value.id.numid = id;
 iowr<ELEM_READ>(value);
 return value.values[0];
}
void AudioControl::operator =(long v) {
 if(!id) { log("No associated control"); return; }
 Value value{.id={.numid = id}, .values = {clamp(min, v, max) /*L*/, clamp(min, v, max)/*R*/}}; iowr<ELEM_WRITE>(value);
}
