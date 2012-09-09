#include "sequencer.h"
#include "stream.h"
#include "file.h"
#include "time.h"
#include "debug.h"
#include "midi.h"
#include "linux.h"

enum { STREAM_INPUT=1 };

/*struct info {
    unsigned int device; //RO/WR (control): device number
    unsigned int subdevice; //RO/WR (control): subdevice number
    int stream; //WR: stream
    int card; //R: card number
    unsigned int flags; //INFO_XXXX
    unsigned char id[64]; //ID (user selectable)
    unsigned char name[80]; //name of device
    unsigned char subname[32]; //name of active or selected subdevice
    unsigned int subdevices_count;
    unsigned int subdevices_avail;
    unsigned char reserved[64]; //reserved for future use
};*/

struct params {
    int stream = STREAM_INPUT;
    long buffer_size = 4096; //queue size in bytes
    long avail_min = 1; //minimum avail bytes for wakeup
    uint no_active_sensing = 0; //do not send active sensing byte in close()
    byte reserved[16]; //reserved for future use
};

/*struct status {
    int stream;
    struct timespec tstamp; //Timestamp
    long avail; //available bytes
    long xruns; //count of overruns since last status (in bytes)
    unsigned char reserved[16]; //reserved for future use
};*/

enum IOCTL {
    //PVERSION = IOR('W', 0x00, int),
    //INFO = IOR('W', 0x01, struct info),
    PARAMS = IOWR('W', 0x10, struct params),
    //STATUS = IOWR('W', 0x20, struct status)
};

Sequencer::Sequencer() : Poll(openFile("/dev/snd/midiC1D0"_),POLLIN) {
    //snd_open(&midi,0,"hw:1,0,0",0); pollfd p; snd_poll_descriptors(midi,&p,1); registerPoll(p.fd,p.events);
    //int ver; ioctl(PVERSION, &ver); log(ver);
    //info info; info.stream=STREAM_INPUT; ioctl(INFO, &info); log(info.name);
    //params params; ioctl(PARAMS, &params);
}

void Sequencer::event() {
    do {
//#define read ({ byte b; check_( snd_read(midi,&b,1) ); b; })
#define read ({ byte b; check_( read(fd,&b,1) ); b; })
        uint8 key=read;
        if(key & 0x80) { type=key>>4; key=read; }
        uint8 value=0;
        if(type == NoteOn || type == NoteOff || type == Aftertouch || type == Controller || type == PitchBend) value=read;
        else warn("Unhandled MIDI event",type);
        if(type == NoteOn) {
            if(value == 0 ) {
                assert(pressed.contains(key));
                pressed.removeAll(key);
                if(sustain) sustained+= key;
                else {
                    noteEvent(key,0);
                    if(record) {
                        int tick = realTime();
                        events << Event((int16)(tick-lastTick), (uint8)key, (uint8)0);
                        lastTick = tick;
                    }
                }
            } else {
                sustained.removeAll(key);
                assert(!pressed.contains(key));
                pressed << key;
                noteEvent(key, min(127,value*3/2)); //x3/2 to use reach maximum velocity without destroying the keyboard
                if(record) {
                    int tick = realTime();
                    events << Event((int16)(tick-lastTick), (uint8)key, (uint8)value);
                    lastTick = tick;
                }
            }
        } else if(type == Controller) {
            if(key==64) {
                sustain = (value != 0);
                if(!sustain) {
                    for(int key : sustained) { noteEvent(key,0); assert(!pressed.contains(key)); }
                    sustained.clear();
                }
            }
        }
    } while(::poll(this,1,0));
}

void Sequencer::recordMID(const ref<byte>& path) { record=File(record,root(),Write); }
Sequencer::~Sequencer() {
    if(!record) return;
    array<byte> track;
    for(Event e : events) {
        int v=e.time;
        if(v >= 0x200000) track << uint8(((v>>21)&0x7f)|0x80);
        if(v >= 0x4000) track << uint8(((v>>14)&0x7f)|0x80);
        if(v >= 0x80) track << uint8(((v>>7)&0x7f)|0x80);
        track << uint8(v&0x7f);
        track << (9<<4) << e.key << e.vel;
    }
    track << 0x00 << 0xFF << 0x2F << 0x00; //EndOfTrack

    struct { char name[4]={'M','T','h','d'}; int32 size=big32(6); int16 format=big16(0);
        int16 trackCount=big16(1); int16 timeDivision=big16(500); } packed MThd;
    write(fd,raw(MThd));
    struct { char name[4]={'M','T','r','k'}; int32 size=0; } packed MTrk; MTrk.size=big32(track.size());
    write(fd,raw(MTrk));
    write(fd,track);
    record.clear();
}
