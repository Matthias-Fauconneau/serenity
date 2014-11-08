#include "midi.h"
#include "file.h"
#include "math.h"

MidiFile::MidiFile(ref<byte> file) { /// parse MIDI header
	BinaryData s(file, true);
    s.advance(10);
    uint16 nofChunks = s.read(); ticksPerSeconds = 2*(uint16)s.read(); // Ticks per second (*2 as actually defined for MIDI as ticks per beat at 120bpm)
    for(int i=0; s && i<nofChunks;i++) {
        ref<byte> tag = s.read<byte>(4); uint32 length = s.read();
        if(tag == "MTrk"_) {
			BinaryData track = copyRef(s.peek(length));
            // Reads first time (next event time will always be kept to read events in time)
            uint8 c=track.read(); uint t=c&0x7f;
            if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|c;}}}
			tracks.append(Track(t, ::move(track)));
        }
        s.advance(length);
    }
    uint minTime=-1; for(Track& track: tracks) minTime=min(minTime,track.time);
    for(Track& track: tracks) {
        track.startTime = track.time-minTime; // Time of the first event
        track.startIndex = track.data.index; // Index of the first event
        read(track);
        duration = max(duration, track.time);
        track.reset();
    }
}

void MidiFile::read(Track& track) {
    BinaryData& s = track.data;
    assert_(s);
    for(;;) {
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

        if(type==NoteOff) type=NoteOn, vel=0;
		if(type==NoteOn) insertSorted(MidiNote{track.time, key, vel});

        if(!s) return;
        uint8 c=s.read(); uint t=c&0x7f;
        if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|c;}}}
        track.time += t;
    }
}
