#include "midi-input.h"
#include "data.h"
#include "file.h"
#include "time.h"
#include "midi.h"
#include "time.h"

Device getMIDIDevice() {
    Folder snd("/dev/snd");
    for(const String& device: snd.list(Devices)) if(startsWith(device, "midi"_)) return Device(device, snd, ReadOnly);
    log("No MIDI device found"); //FIXME: Block and watch folder until connected
    return Device();
}

MidiInput::MidiInput(Thread& thread) : Device(getMIDIDevice()), Poll(Device::fd,POLLIN,thread) {}

void MidiInput::event() {
    while(Stream::fd && poll()) {
        uint8 key=read<uint8>();
        if(key & 0x80) { type=key>>4; key=read<uint8>(); }
        uint8 value=0;
        if(type == NoteOn || type == NoteOff || type == Aftertouch || type == Controller || type == PitchBend) value = read<uint8>();
        else log("Unhandled MIDI event"_, type);
        if(type == NoteOn) {
            if(value == 0 ) {
                if(!pressed.contains(key)) return; // Pressed before the device was opened
                pressed.remove(key);
                if(sustain) sustained.add( key );
                else noteEvent(key,0,1);
            } else {
                sustained.tryRemove(key);
                assert_(!pressed.contains(key));
                pressed.append(key);
                noteEvent(key, min(127,(int)value*4/3), 1); // Keyboard saturates at 96
            }
        } else if(type == Controller) {
            if(key==64) {
                sustain = (value != 0);
                if(!sustain) {
                    for(int key : sustained) { noteEvent(key,0,1); assert(!pressed.contains(key)); }
                    sustained.clear();
                }
            }
        }
    }
}
