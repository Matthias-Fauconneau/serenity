#include "music.h"
#include "file.h"

void MidiFile::open(const string& path) { /// parse MIDI header
	Stream s = mapFile(path);
	s+=10;
	uint16 nofChunks = s.read();
	midiClock = 48*60000/120/(uint16)s.read(); //48Khz clock
	for(int i=0; s && i<nofChunks;i++) {
		string tag = s.read(4); uint32 length = s.read();
		if(tag == _("MTrk")) {
			int i=0; while(s[i]&0x80) i++; //ignore first time to revert decode order
			tracks << Track(s.slice(i,length));
		}
		s += length;
	}
}

void MidiFile::read(Track& track, int time, State state) {
	if(!track) return;
	while(track.time < time) {
		int type=track.type, vel=0, key=*track++;
		if(key & 0x80) { type=key>>4; key=*track++; }
		if( type == NoteOn) vel=*track++;
		else if( type == NoteOff || type == Aftertouch || type == Controller || type == PitchBend ) track++;
		else if( type == ProgramChange || type == ChannelAftertouch ) {}
		else if( type == Meta ) {
			uint8 c=*track++; int len=c&0x7f; if(c&0x80){ c=*track++; len=(len<<7)|(c&0x7f); }
			track+=len;
		}
		track.type = type;

		if(state==Play) {
			if(type==NoteOn) noteEvent.emit(key,vel);
			else if(type==NoteOff) noteEvent.emit(key,0);
		}/* else if(state==Sort) {
			sort[e.tick][e.note] =
		}*/

		if(!track) return;
		uint8 c=*track++; int t=c&0x7f;
		if(c&0x80){c=*track++;t=(t<<7)|(c&0x7f);if(c&0x80){c=*track++;t=(t<<7)|(c&0x7f);if(c&0x80){c=*track++;t=(t<<7)|c;}}}
		track.time += t*midiClock;
	}
}

void MidiFile::seek(int time) {
	for(int i=0;i<tracks.size;i++) { Track& track=tracks[i];
		if(time < track.time) { track.time=0; track.i=0; }
		read(track,time,Seek);
		track.time -= time;
	}
}
void MidiFile::update(int time) {
	for(int i=0;i<tracks.size;i++) read(tracks[i],time,Play);
}
