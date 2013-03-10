#include "midi.h"
#include "file.h"

void MidiFile::open(const ref<byte>& data) { /// parse MIDI header
    clear();
    BinaryData s(data,true);
    s.advance(10);
    uint16 nofChunks = s.read(); ticksPerBeat = s.read();
    for(int i=0; s && i<nofChunks;i++) {
        uint32 tag = s.read<uint32>(); uint32 length = s.read();
        if(tag == raw<uint32>("MTrk"_)) {
            BinaryData track = array<byte>(s.peek(length));
            uint8 c=track.read(); uint t=c&0x7f;
            if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|c;}}}
            tracks<< Track(t,move(track));
        }
        s.advance(length);
    }
    uint64 minTime=-1; for(Track& track: tracks) minTime=min(minTime,track.time);
    for(Track& track: tracks) { track.startTime=track.time-minTime, track.startIndex=track.data.index; read(track,-1,Sort); track.reset(); }
    seek(0);
}

void MidiFile::read(Track& track, uint64 time, State state) {
    BinaryData& s = track.data;
    if(!s) return;
    while(track.time*(48*60000/120)/ticksPerBeat < time) {
        uint8 key=s.read();
        if(key & 0x80) { track.type_channel=key; key=s.read(); }
        uint8 type=track.type_channel>>4; //uint8 channel=track.type_channel&0b1111;
        uint8 vel=0;
        if(type == NoteOn) vel=s.read();
        else if(type == NoteOff || type == Aftertouch || type == Controller || type == PitchBend ) s.advance(1);
        else if(type == ProgramChange || type == ChannelAftertouch ) {}
        else if(type == Meta) {
            uint8 c=s.read(); uint len=c&0x7f; if(c&0x80){ c=s.read(); len=(len<<7)|(c&0x7f); }
            enum { SequenceNumber, Text, Copyright, TrackName, InstrumentName, Lyrics, Marker, Cue, ChannelPrefix=0x20,
                   EndOfTrack=0x2F, Tempo=0x51, Offset=0x54, TimeSignature=0x58, KeySignature, SequencerSpecific=0x7F };
            ref<byte> data = s.read<byte>(len);
            if(key==TimeSignature) timeSignature[0]=data[0], timeSignature[1]=1<<data[1];
            else if(key==Tempo) tempo=((data[0]<<16)|(data[1]<<8)|data[2])/1000;
            else if(key==KeySignature) this->key=(int8)data[0], scale=data[1]?Minor:Major;
        }

        if((type==NoteOff || type==NoteOn) && active.contains(key)) {
            if(state==Play) noteEvent(key,0);
            uint start = active.take(key);
            if(state==Sort) {
                uint duration = track.time-start;
                notes.sorted(start).insertSorted(MidiNote{key, start*4/ticksPerBeat, duration*4.f/ticksPerBeat/**10/9*/});
            }
        }
        if(type==NoteOn && vel) {
            if(!active.contains(key)) {
                if(state==Play) noteEvent(key,vel);
                active.insert(key,track.time);
            }
        }

        if(!s) return;
        uint8 c=s.read(); uint t=c&0x7f;
        if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|c;}}}
        track.time += t;
    }
}

void MidiFile::seek(uint64 time) {
    active.clear();
    for(Track& track: tracks) {
        if(time < track.time*(48*60000/120)/ticksPerBeat) track.reset();
        read(track,time,Seek);
    }
    this->time=time;
}
void MidiFile::update(uint delta) {
    this->time+=delta;
    for(Track& track: tracks) read(track,time,Play);
}
