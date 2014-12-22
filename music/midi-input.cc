#include "midi-input.h"
#include "data.h"
#include "file.h"
#include "time.h"
#include "midi.h"
#include "time.h"

Device getMIDIDevice() {
    Folder snd("/dev/snd");
    for(const String& device: snd.list(Devices)) if(startsWith(device, "midi"_)) return Device(device, snd, ReadOnly);
    error("No MIDI device found"); //FIXME: Block and watch folder until connected
}

MidiInput::MidiInput(Thread& thread) : Device(getMIDIDevice()), Poll(Device::fd,POLLIN,thread) {}

void MidiInput::event() {
    if(!(revents&POLLIN)) return;
    uint8 key=read<uint8>();
    if(key & 0x80) { type=key>>4; key=read<uint8>(); }
    uint8 value=0;
    if(type == NoteOn || type == NoteOff || type == Aftertouch || type == Controller || type == PitchBend) value=read<uint8>();
    else log("Unhandled MIDI event"_,type);
    if(type == NoteOn) {
        if(value == 0 ) {
            if(!pressed.contains(key)) return; //pressed even before the device was opened
            pressed.remove(key);
            if(sustain) sustained.add( key );
            else {
                noteEvent(key,0);
                /*if(record) {
                    int tick = realTime()/1000000;
                    events.append( Event((int16)(tick-lastTick), (uint8)key, (uint8)0) );
                    lastTick = tick;
                }*/
            }
        } else {
            sustained.tryRemove(key);
            assert_(!pressed.contains(key));
            pressed.append(key);
            noteEvent(key, min(127,(int)value*4/3)); // Keyboard saturates at 96
            /*if(record) {
                int tick = realTime()/1000000;
                events.append( Event((int16)(tick-lastTick), (uint8)key, (uint8)value) );
                lastTick = tick;
            }*/
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

/*void MidiInput::recordMID(const string& path) {
    if(existsFile(path,home())) { log(path,"already exists"); return; }
    record=File(path,home(),Flags(WriteOnly|Create|Truncate));
}
MidiInput::~MidiInput() {
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
    record.write(raw(MThd));
    struct { char name[4]={'M','T','r','k'}; int32 size=0; } packed MTrk; MTrk.size=big32(track.size);
    record.write(raw(MTrk));
    record.write(track);
}*/
