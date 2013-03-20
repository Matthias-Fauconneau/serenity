#include "sequencer.h"
#include "data.h"
#include "file.h"
#include "time.h"
#include "midi.h"
#include "time.h"

Sequencer::Sequencer(Thread& thread) : Device("/dev/snd/midiC1D0"_,ReadOnly), Poll(Device::fd,POLLIN,thread) { registerPoll(); }

void Sequencer::event() {
    uint8 key=read<uint8>();
    if(key & 0x80) { type=key>>4; key=read<uint8>(); }
    uint8 value=0;
    if(type == NoteOn || type == NoteOff || type == Aftertouch || type == Controller || type == PitchBend) value=read<uint8>();
    else log("Unhandled MIDI event"_,type);
    if(type == NoteOn) {
        if(value == 0 ) {
            if(!pressed.contains(key)) return; //pressed even before the device was opened
            pressed.removeAll(key);
            if(sustain) sustained += key;
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
            noteEvent(key, min(127,(int)value*4/3)); //x4/3 to reach maximum velocity without destroying the keyboard
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
}

void Sequencer::recordMID(const ref<byte>& path) {
    if(existsFile(path,home())) { log(path,"already exists"); return; }
    record=File(path,home(),WriteOnly|Create|Truncate);
}
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
    write(raw(MThd));
    struct { char name[4]={'M','T','r','k'}; int32 size=0; } packed MTrk; MTrk.size=big32(track.size);
    write(raw(MTrk));
    write(track);
}
