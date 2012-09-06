#include "sequencer.h"
#include "stream.h"
#include "file.h"
#include "time.h"
#include "debug.h"
#include "midi.h"
#include "linux.h"

extern "C" int snd_rawmidi_open(snd_rawmidi_t **in_rmidi, snd_rawmidi_t **out_rmidi, const char *name, int mode);
extern "C" int snd_rawmidi_poll_descriptors(snd_rawmidi_t *rmidi, struct pollfd *pfds, unsigned int space);
extern "C" long snd_rawmidi_read(snd_rawmidi_t *rmidi, void *buffer, long size);

Sequencer::Sequencer() {
    snd_rawmidi_open(&midi,0,"hw:1,0,0",0);
    pollfd p; snd_rawmidi_poll_descriptors(midi,&p,1); registerPoll(p.fd,p.events);
}

void Sequencer::event() {
    do {
#define read ({ byte b; snd_rawmidi_read(midi,&b,1); b; })
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
                noteEvent(key, value*3/2); //x3/2 to use reach maximum velocity without destroying the keyboard
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

void Sequencer::recordMID(const ref<byte>& path) { record=string(path); }
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

    File fd = createFile(record);
    struct { char name[4]={'M','T','h','d'}; int32 size=big32(6); int16 format=big16(0);
        int16 trackCount=big16(1); int16 timeDivision=big16(500); } packed MThd;
    write(fd,raw(MThd));
    struct { char name[4]={'M','T','r','k'}; int32 size=0; } packed MTrk; MTrk.size=big32(track.size());
    write(fd,raw(MTrk));
    write(fd,track);
    record.clear();
}
