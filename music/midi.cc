#include "midi.h"
#include "file.h"
#include "math.h"

MidiFile::MidiFile(const ref<byte>& data) { /// parse MIDI header
    clear();
    BinaryData s(data,true);
    s.advance(10);
    uint16 nofChunks = s.read(); ticksPerBeat = s.read();
    for(int i=0; s && i<nofChunks;i++) {
        ref<byte> tag = s.read<byte>(4); uint32 length = s.read();
        if(tag == "MTrk"_) {
            BinaryData track = array<byte>(s.peek(length));
            // Reads first time (next event time will always be kept to read events in time)
            uint8 c=track.read(); uint t=c&0x7f;
            if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|c;}}}
            tracks<< Track(t,move(track));
        }
        s.advance(length);
    }
    uint minTime=-1; for(Track& track: tracks) minTime=min(minTime,track.time);
    for(Track& track: tracks) {
        track.startTime = track.time-minTime; // Time of the first event
        track.startIndex = track.data.index; // Index of the first event
        read(track,-1,Sort);
        duration = max(duration, uint(track.time*uint64(48*60000/120)/ticksPerBeat));
        track.reset();
    }
    seek(0);
}

void MidiFile::read(Track& track, uint time, State state) {
    BinaryData& s = track.data;
    if(!s) { endOfFile(); return; }
    while(uint(track.time*uint64(48*60000/120)/ticksPerBeat) < time) {
        uint8 key=s.read();
        if(key & 0x80) { track.type_channel=key; key=s.read(); }
        uint8 type=track.type_channel>>4;
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
            MidiNote note = active.take(active.indexOf(key));
            if(state==Sort) {
                uint nearest = 0;
                for(const MidiNote& o: notes) { if(abs(int(o.time-note.time))<abs(int(note.time-nearest))) nearest=o.time; }
                notes.insertSorted(MidiNote{note.time, note.key, note.velocity});
            }
        }
        if(type==NoteOn && vel) {
            if(!active.contains(key)) {
                if(state==Play) noteEvent(key,vel);
                active.append(MidiNote{track.time, key, vel});
            }
        }

        if(!s) return;
        uint8 c=s.read(); uint t=c&0x7f;
        if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|c;}}}
        track.time += t;
    }
}

void MidiFile::seek(uint time) {
    active.clear();
    for(Track& track: tracks) {
        if(time < track.time*(48*60000/120)/ticksPerBeat) track.reset();
        read(track,time,Seek);
    }
    this->time=time;
}

void MidiFile::read(uint time) {
    for(Track& track: tracks) read(track, time, Play);
    this->time = time;
}
