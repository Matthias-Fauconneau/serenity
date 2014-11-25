#include "midi.h"
#include "file.h"
#include "math.h"

MidiFile::MidiFile(ref<byte> file) { /// parse MIDI header
	BinaryData s(file, true);
    s.advance(10);
	uint16 nofChunks = s.read();
	notes.ticksPerSeconds = 2*(uint16)s.read(); // Ticks per second (*2 as actually defined for MIDI as ticks per beat at 120bpm)
	divisions = notes.ticksPerSeconds; // Defaults to 60 qpm (FIXME: max 64/metronome.perMinute)
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
	for(size_t index: range(tracks.size)) {
		Track& track = tracks[index];
        track.startTime = track.time-minTime; // Time of the first event
        track.startIndex = track.data.index; // Index of the first event
		this->read(track, index);
        duration = max(duration, track.time);
        track.reset();
    }
}

void MidiFile::read(Track& track, uint trackIndex) {
	BinaryData& s = track.data;
	map<uint, Clef> clefs;
	KeySignature keySignature={0}; TimeSignature timeSignature={4,4}; Metronome metronome={Quarter, 120};
	uint measureIndex = 0;
	int64 lastMeasureStart = 0;
	map<uint, int64> active;
	while(s) {
		uint8 key=s.read();
		if(key & 0x80) { track.type_channel=key; key=s.read(); }
		uint8 type=track.type_channel>>4;
		uint8 vel=0;
		if(type == NoteOn) vel=s.read();
		else if(type == NoteOff) { vel=s.read(); assert_(vel==0) ; }
		else if(/*type == Aftertouch ||*/ type == Controller/*TODO: pedal*/ /*|| type == PitchBend*/) s.advance(1);
		else if(type == ProgramChange /*|| type == ChannelAftertouch*/) {}
		else if(type == Meta) {
			uint8 c=s.read(); uint len=c&0x7f; if(c&0x80){ c=s.read(); len=(len<<7)|(c&0x7f); }
			enum class MIDI { SequenceNumber, Text, Copyright, TrackName, InstrumentName, Lyrics, Marker, Cue, ChannelPrefix=0x20,
				   EndOfTrack=0x2F, Tempo=0x51, Offset=0x54, TimeSignature=0x58, KeySignature, SequencerSpecific=0x7F };
			ref<byte> data = s.read<byte>(len);
			if(MIDI(key)==MIDI::TimeSignature) {
				uint beats = data[0];
				uint beatUnit = 1<<data[1];
				timeSignature = TimeSignature{beats, beatUnit};
				signs.insertSorted(Sign{track.time, 0, uint(-1), Sign::TimeSignature, .timeSignature=timeSignature});
			}
			else if(MIDI(key)==MIDI::Tempo) {
				uint tempo=((data[0]<<16)|(data[1]<<8)|data[2]); // Microseconds per beat (quarter)
				uint perMinute = 60000000 / tempo; // Beats per minute
				signs.insertSorted({track.time, 0, uint(-1), Sign::Metronome, .metronome={Quarter, perMinute}});
			}
			else if(MIDI(key)==MIDI::KeySignature) {
				keySignature.fifths=(int8)data[0];
				//scale=data[1]?Minor:Major;
				signs.insertSorted(Sign{track.time, 0, uint(-1), Sign::KeySignature, .keySignature=keySignature});
			}
			else if(MIDI(key)==MIDI::TrackName || MIDI(key)==MIDI::Text || MIDI(key)==MIDI::Copyright) log(data);
			else if(MIDI(key)==MIDI::EndOfTrack) {}
			else error(hex(key));
		}
		else error(type);

		if(type==NoteOff) type=NoteOn, vel=0;
		if(type==NoteOn) {
			notes.insertSorted(MidiNote{track.time, key, vel});
			if(active.contains(key)) {
				//assert(key==-1 || key==0 || key==2, key);
				assert_(keySignature.fifths == 0);
				int keys[4][11] = {
					{1,0,1,0,1,1,0,1,0,1,0}, // F minor (♭)
					{0,1,0,1,1,0,1,0,1,0,1},  // C major (♮) (should be same for all majors)
					{0,1,0,1,1,0,1,0,1,0,1},  // G major (♯) TESTME
					{0,1,0,1,1,0,1,0,1,0,1}  // D major (♯♯) TESTME
				};
				int accidentals[4][12] = { // 0=nothing, 1=♭, 2=♮, 3=♯ (TODO)
										   {0,1,0,1,0,0,1,0,1,0,0,2}, // F minor (♭)
										   {0,3,0,3,0,0,3,0,3,0,3,0},  // C major (♮)
										   {0,3,0,3,0,2,0,0,3,0,3,0},  // G major (♯) TODO
										   {2,0,0,3,0,2,0,0,3,0,3,0}  // D major (♯♯) TODO
										 };
				int h=key/12*7; for(int i=0;i<key%12;i++) h+=keys[keySignature.fifths+1][i];
				const int trebleOffset = 6*7+3; // C0 position in intervals from top line
				const int bassOffset = 4*7+5; // C0 position in intervals from top line
				uint staff = key < 60; // C4
#if 0 //TODO: Balances load on both hand
				array<MidiNote> currents[2]; // new notes to be pressed
				for(MidiNote note: notes.values[i]) { //first rough split based on pitch
					int s = note.; //middle C
					currents[s] << note;
					actives[s] << note;
				}
				for(uint s: range(2)) { // then balances load on both hand
					array<MidiNote>& active = actives[s];
					array<MidiNote>& otherActive = actives[!s];
					array<MidiNote>& current = currents[s];
					array<MidiNote>& other = currents[!s];
					while(
						  current && // any notes to move ?
						  ((s==0 && current.last().key>=52) || (s==1 && current.first().key<68)) && // prevent stealing from far notes (TODO: relative to last active)
						  current.size>=other.size && // keep new notes balanced
						  active.size>=otherActive.size && // keep active (sustain+new) notes balanced
						  (!other ||
						   (s==1 && abs(int(other.first().key-current.first().key))<=12) || // keep short span on new notes (left)
						   (s==0 && abs(int(other.last().key-current.last().key))<=12) ) && // keep short span on new notes (right)
						  (!sustain[!s] ||
						   (s==1 && abs(int(otherActive[0].key-current.first().key))<=18) || // keep short span with active notes (left)
						   (s==0 && abs(int(otherActive[sustain[!s]-1].key-current.last().key))<=18) ) && // keep short span with active notes (right)
						  (
							  active.size>otherActive.size+1 || // balance active notes
							  current.size>other.size+1 || // balance load
							  // both new notes and active notes load are balanced
							  (currents[0] && currents[1] && s == 0 && abs(int(currents[1].first().key-currents[1].last().key))<abs(int(currents[0].first().key-currents[0].last().key))) || // minimize left span
							  (currents[0] && currents[1] && s == 1 && abs(int(currents[0].first().key-currents[0].last().key))<abs(int(currents[1].first().key-currents[1].last().key))) || // minimize right span
							  (sustain[s] && sustain[!s] && active[sustain[s]-1].start>otherActive[sustain[!s]-1].start) // load least recently used hand
							  )) {
						if(!s) {
							other.insertAt(0, current.pop());
							actives[!s].insertAt(0, active.pop());
						} else {
							other << current.take(0);
							actives[!s] << active.take(sustain[s]);
						}
					}
				}
#endif
				Clef clef = clefs.value(staff, {Treble, 0});
				assert_(!clef.octave);
				int clefOffset = clef.clefSign==Treble ? trebleOffset : clef.clefSign==Bass ? bassOffset : 0;
				int noteStep = clefOffset-h;
				int accidentalShortCode = accidentals[keySignature.fifths+1][key%12]; // FIXME: single code
				Accidental noteAccidental = Accidental(accidentalShortCode ? Flat-1+accidentalShortCode : None);
				log(track.time, strKey(key), staff);
				int duration = track.time-active[key];
				log(duration, duration*metronome.perMinute/60./divisions, duration*metronome.perMinute/60/divisions);
				uint typeDurations[] = {64,32,16,8,4,2,1};
				const uint quarterDuration = 16*metronome.perMinute/60;
				uint typeDuration = duration*quarterDuration/divisions;
				assert_(typeDuration);
				bool dot=false;
				if(typeDuration%3 == 0) {
					dot = true;
					typeDuration = typeDuration * 2 / 3;
				}
				Duration type = Duration(ref<uint>(typeDurations).size-1-log2(typeDuration));
				assert_(int(type) >= 0, duration, typeDuration);
				signs.insertSorted(Sign{active[key], duration, staff, Sign::Note, .note={
											clef, noteStep, noteAccidental, type, Note::NoTie,
											dot, /*grace*/ false, /*slash*/ false, /*staccato*/ false, /*tenuto*/ false, /*accent*/ false, /*trill*/ false, /*up*/false,
											key, invalid, invalid}});
				active.remove(key);
			}
			if(vel) active[key] = track.time;
		}

		if(s) {
			uint8 c=s.read(); uint t=c&0x7f;
			if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|c;}}}
			track.time += t;
		}

		if(trackIndex /*First track is metadata*/) {
			int64 measureLength = timeSignature.beats*60*divisions/metronome.perMinute;
			assert_(measureLength);
			int64 nextMeasureStart = lastMeasureStart+measureLength;
			if(track.time >= nextMeasureStart) {
				lastMeasureStart = nextMeasureStart;
				signs.insertSorted({nextMeasureStart, 0, uint(-1), Sign::Measure, .measure={measureIndex, 1, 1, measureIndex}});
				measureIndex++;
			}
		}
	}
	assert_(!active);
}
