#include "midi.h"
#include "file.h"

#include "array.cc"
template class array<Track>;

void MidiFile::open(const string& path) { /// parse MIDI header
    DataBuffer s( readFile(path) );
    s.advance(10);
    uint16 nofChunks = s.read();
    midiClock = 48*60000/120/(uint16)s.read(); //48Khz clock
    for(int i=0; s && i<nofChunks;i++) {
        string tag = s.read(4); uint32 length = s.read();
        if(tag == "MTrk"_) {
            while(s.read<byte>()&0x80) {} //ignore first time
            tracks.append( Track(s.read(length)) );
        }
        s.advance(length);
    }
}

void MidiFile::read(Track& track, int time, State state) {
    DataStream& s = track.stream;
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
            if(type==NoteOn) noteEvent.emit(key,vel);
            else if(type==NoteOff) noteEvent.emit(key,0);
        }/* else if(state==Sort) {
            sort[e.tick][e.note] =
        }*/

        if(!s) return;
        uint8 c=s.read(); int t=c&0x7f;
        if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|c;}}}
        track.time += t*midiClock;
    }
}

void MidiFile::seek(int time) {
    for(Track& track: tracks) {
        if(time < track.time) { track.time=0; track.stream.index=0; }
        read(track,time,Seek);
        track.time -= time;
    }
}
void MidiFile::update(int time) {
    for(Track& track: tracks) read(track,time,Play);
}
