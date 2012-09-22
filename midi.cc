#include "midi.h"
#include "file.h"

void MidiFile::open(const ref<byte>& data) { /// parse MIDI header
    BinaryData s(data,true);
    s.advance(10);
    uint16 nofChunks = s.read(), clockUnit = s.read();
    midiClock = 48*60000/120/clockUnit; //48Khz clock
    for(int i=0; s && i<nofChunks;i++) {
        uint32 tag = s.read<uint32>(); uint32 length = s.read();
        if(tag == raw<uint32>("MTrk"_)) {
            BinaryData track = array<byte>(s.peek(length));
            uint8 c=track.read(); uint t=c&0x7f;
            if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|c;}}}
            tracks<< Track(t*midiClock,move(track));
        }
        s.advance(length);
    }
    uint minTime=-1; for(Track& track: tracks) minTime=min(minTime,track.time);
    for(Track& track: tracks) { int time=track.time-minTime,index=track.data.index; read(track,-1,Sort); track.time=time; track.data.index=index; }
}

void MidiFile::read(Track& track, uint time, State state) {
    BinaryData& s = track.data;
    if(!s) return;
    while(track.time < time) {
        uint8 type=track.type, vel=0,key=s.read();
        if(key & 0x80) { type=key>>4; key=s.read(); }
        if( type == NoteOn) vel=s.read();
        else if( type == NoteOff || type == Aftertouch || type == Controller || type == PitchBend ) s.advance(1);
        else if( type == ProgramChange || type == ChannelAftertouch ) {}
        else if( type == Meta ) {
            uint8 c=s.read(); int len=c&0x7f; if(c&0x80){ c=s.read(); len=(len<<7)|(c&0x7f); }
            s.advance(len);
        }
        track.type = type;

        if(state==Play) {
            if(type==NoteOn) noteEvent(key,vel);
            else if(type==NoteOff) noteEvent(key,0);
        } else if(state==Sort) {
            if(type==NoteOn && vel) notes.sorted(track.time).insertSorted(key);
        }

        if(!s) return;
        uint8 c=s.read(); uint t=c&0x7f;
        if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|c;}}}
        track.time += t*midiClock;
    }
}

void MidiFile::seek(uint time) {
    for(Track& track: tracks) {
        if(time < track.time) { track.time=0; track.data.index=0; }
        read(track,time,Seek);
    }
    this->time=time;
}
void MidiFile::update(uint delta) {
    this->time+=delta;
    for(Track& track: tracks) read(track,time,Play);
}
