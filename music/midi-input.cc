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

MidiInput::MidiInput(Thread& thread) : Device(getMIDIDevice()), Poll(Device::fd,POLLIN,thread),
  min(existsFile("keyboard.min",".config"_) ? apply(split(readFile("keyboard.min",".config"_).slice(1)," "), [](string s)->int{return parseInteger(s);}) : apply(88, [](int){return 127;})),
  max(existsFile("keyboard",".config"_) ? apply(split(readFile("keyboard",".config"_).slice(1)," "), [](string s)->int{return parseInteger(s);}) : apply(88, [](int){return 1;}))
{ assert_(min.size == 88,min.size,min,readFile("keyboard.min",".config"_).slice(1)); /*log(min);*/
   assert_(max.size == 88,max.size,max,readFile("keyboard",".config"_).slice(1)); /*log(max);*/ }
  MidiInput::~MidiInput() {
   writeFile("keyboard.min", str(min), ".config"_, true); log(min);
   writeFile("keyboard", str(max), ".config"_, true); log(max);
  }

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
                if(sustain) sustained.add(key);
                else noteEvent(key,0);
            } else {
                sustained.tryRemove(key);
                assert_(!pressed.contains(key));
                pressed.append(key);
                //if(value*4/3 > 127) log(key, value); noteEvent(key, min(127,(int)value*4/3)); // Keyboard saturates at 96
                assert_(key >= 21 && key < 21+88);
                int& min = this->min[key-21];
                if(value < min) min = value;
                int& max = this->max[key-21];
                if(value > max) max = value;
                int MIN = ::min(min, 32);
                if(value < MIN) value=MIN;
                noteEvent(key, ::min(127,1+(int)(value-MIN)*128/(max-MIN)));
                //noteEvent(key, value);
            }
        } else if(type == Controller) {
            if(key==64) {
                ccEvent(key, value);
                sustain = (value != 0);
                if(!sustain) {
                    for(int key : sustained) { noteEvent(key,0); assert(!pressed.contains(key)); }
                    sustained.clear();
                }
            }
        }
    }
}
