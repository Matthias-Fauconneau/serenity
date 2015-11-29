#include "midi.h"
#include "file.h"
#include "math.h"

inline uint parseKey(TextData& s) {
 int key=24;
 if(!"cdefgabCDEFGAB"_.contains(s.peek())) return -1;
 key += "c#d#ef#g#a#b"_.indexOf(lowerCase(s.next()));
 if(s.match('#')) key++;
 key += 12*s.mayInteger(4);
 return key;
}
inline uint parseKey(const string name) { TextData s(name); return parseKey(s); }

MidiFile::MidiFile(ref<byte> file) { /// parse MIDI header
 BinaryData s(file, true);
 s.advance(10);
 uint16 nofChunks = s.read();
 notes.ticksPerSeconds = 2*(uint16)s.read(); // Ticks per second (*2 as actually defined for MIDI as ticks per beat at 120bpm)
 divisions = notes.ticksPerSeconds; // Defaults to 60 qpm (FIXME: max 64/metronome.perMinute)

 for(int i=0; s && i<nofChunks;i++) {
  ref<byte> tag = s.read<byte>(4); uint32 length = s.read();
  if(tag == "MTrk"_) {
   BinaryData track (s.peek(length));
   // Reads first time (next event time will always be kept to read events in time)
   uint8 c=track.read(); uint t=c&0x7f;
   if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=track.read();t=(t<<7)|c;}}}
   //tracks.append(copyRef(track.data), t);
   tracks.append(::move(track), t);
  }
  s.advance(length);
 }

 {
  uint minTime=-1;
  for(Track& track: tracks) minTime=min(minTime,track.time);
  for(Track& track: tracks) {
   track.startTime = track.time-minTime; // Time of the first event
   track.startIndex = track.data.index; // Index of the first event
   duration = max(duration, track.time);
   track.reset();
  }
 }

 KeySignature keySignature = 0;
 TimeSignature timeSignature = {4,4};
 Metronome metronome = {Quarter, 120};
 uint lastTempoChange = 0, lastTempoChange120 = 0; // Last tempo changes in file ticks and in 120bpm ticks
 uint measureIndex = 0;
 int lastMeasureStart = 0;
 Clef clefs[2] = {{FClef,0}, {GClef,0}};
 for(uint staff: range(2)) signs.insertSorted({Sign::Clef, 0, {{staff, {.clef=clefs[staff]}}}});
 array<MidiNote> currents[2]; // New notes to be pressed
 array<MidiNote> actives[2]; // Notes currently pressed
 array<MidiNote> commited[2]; // Commited/assigned notes to be written once duration is known (on note off)
 uint lastOff[2] = {0,0}; // For rests
 for(uint lastTime = 0;;) {
  size_t trackIndex = invalid;
  for(size_t index: range(tracks.size))
   if(tracks[index].data && (trackIndex==invalid || tracks[index].time <= tracks[trackIndex].time))
    trackIndex = index;
  if(trackIndex == invalid) break;
  Track& track = tracks[trackIndex];
  assert_(track.time >= lastTime);

  if(track.time != lastTime) { // Commits last chord
   uint sustain[2] = { (uint)actives[0].size, (uint)actives[1].size }; // Remaining notes kept sustained
   // Balances load on both hand
   for(size_t staff: range(2)) {
    array<MidiNote>& active = actives[staff];
    array<MidiNote>& otherActive = actives[!staff];
    array<MidiNote>& current = currents[staff];
    array<MidiNote>& other = currents[!staff];
    if(0)
     while(
           current && // Any notes to move from staff to !staff ?
           ((staff==0 && current.last().key>parseKey("F2")) || (staff==1 && current.first().key<68)) && // Prevents stealing from far notes (TODO: relative to last active)
           current.size>=other.size && // keep new notes balanced
           active.size>=otherActive.size && // keep active (sustain+new) notes balanced
           (!other ||
            (staff==1 && abs(int(other.first().key-current.first().key))<=12) || // Keeps short span on new notes (left)
            (staff==0 && abs(int(other.last().key-current.last().key))<=12) ) && // Keeps short span on new notes (right)
           (!sustain[!staff] ||
            (staff==1 && abs(int(otherActive[0].key-current.first().key))<=18) || // Keeps short span with active notes (left)
            (staff==0 && abs(int(otherActive[sustain[!staff]-1].key-current.last().key))<=18) ) && // Keeps short span with active notes (right)
           (
            active.size>otherActive.size+1 || // Balances active notes
            current.size>other.size+1 || // Balances load
            // Both new notes and active notes load are balanced
            (currents[0] && currents[1] && staff == 0 && abs(int(currents[1].first().key-currents[1].last().key))<abs(int(currents[0].first().key-currents[0].last().key))) || // Minimizes left span
            (currents[0] && currents[1] && staff == 1 && abs(int(currents[0].first().key-currents[0].last().key))<abs(int(currents[1].first().key-currents[1].last().key))) || // Minimizes right span
            (sustain[staff] && sustain[!staff] && active.last()/*[sustain[staff]-1]?*/.time>otherActive.last()/*[sustain[!staff]-1]?*/.time) // Loads least recently used hand
            )) {
      // Notes are sorted bass to treble (bottom to top)
      if(!staff) { // Top bass to bottom treble
       other.insertAt(0, current.pop());
       otherActive.insertAt(0, active.pop());
       //assert_(other[0]==otherActive[0]); FIXME
      } else { // Bottom treble to top bass
       //assert_(current[0] == active[0], current, active, sustain[staff]);
       other.append( current.take(0) );
       size_t index = active.indexOf(other.last()) /*sustain[staff]?*/;
       if(index != invalid) { // FIXME
        assert_(index != invalid);
        otherActive.append(active.take(index));
       }
      }
     }
   }
   for(size_t staff: range(2)) commited[staff].append(::move(currents[staff]));  // Defers until duration is known (on note off)
   for(size_t staff: range(2)) currents[staff].clear(); // FIXME: ^ move should already clear currents[staff]
   for(size_t staff: range(2)) assert_(!currents[staff], currents, commited);
   /*if(measureIndex > 35) {
                signs.insertSorted({Sign::Measure, track.time, .measure={Measure::NoBreak, measureIndex, 1, 1, measureIndex}});
                break; // HACK: Stops 'Brave Adventurers' before repeat
            }*/
  }

  lastTime = track.time;

  // When latest track is ready to switch measure
  int measureLength = timeSignature.beats*60*divisions/metronome.perMinute;
  assert_(measureLength);
  uint nextMeasureStart = lastMeasureStart+measureLength;
  if(track.time >= nextMeasureStart) {
   lastMeasureStart = nextMeasureStart;
   signs.insertSorted({Sign::Measure, nextMeasureStart, .measure={Measure::NoBreak, measureIndex, 1, 1, measureIndex}});
   measureIndex++;
  }

  BinaryData& s = tracks[trackIndex].data;
  uint8 key=s.read();
  if(key & 0x80) { track.type_channel=key; key=s.read(); }
  uint8 type=track.type_channel>>4;
  uint8 vel=0;
  if(type == NoteOn) vel=s.read();
  else if(type == NoteOff) { vel=s.read(); /*assert_(vel==0, vel) ;*/ }
  else if(/*type == Aftertouch ||*/ type == Controller/*TODO: pedal*/ /*|| type == PitchBend*/) s.advance(1);
  else if(type == ProgramChange /*|| type == ChannelAftertouch*/) {}
  else if(type == Meta) {
   uint8 c=s.read(); uint len=c&0x7f; if(c&0x80){ c=s.read(); len=(len<<7)|(c&0x7f); }
   enum class MIDI { SequenceNumber, Text, Copyright, TrackName, InstrumentName, Lyrics, Marker, Cue, ChannelPrefix=0x20,
                     EndOfTrack=0x2F, Tempo=0x51, Offset=0x54, TimeSignature=0x58, KeySignature, SequencerSpecific=0x7F };
   ref<uint8> data = s.read<uint8>(len);
   if(MIDI(key)==MIDI::TimeSignature) {
    uint beats = data[0];
    uint beatUnit = 1<<data[1];
    timeSignature = TimeSignature{beats, beatUnit};
    signs.insertSorted({Sign::TimeSignature, track.time, .timeSignature=timeSignature});
   }
   else if(MIDI(key)==MIDI::Tempo) {
    int tempo = ((data[0]<<16)|(data[1]<<8)|data[2]); // Microseconds per beat (quarter)
    metronome.perMinute = 60000000 / tempo; // Beats per minute
    //if(perMinute) signs.insertSorted({track.time, 0, uint(-1), Sign::Metronome, .metronome={Quarter, perMinute}});
    log("Tempo", track.startTime, track.time, tempo, metronome.perMinute, notes.ticksPerSeconds);
    lastTempoChange120 += (track.time - lastTempoChange) * 120 / metronome.perMinute; // FIXME: use microseconds per beat
    lastTempoChange = track.time;
   }
   else if(MIDI(key)==MIDI::KeySignature) {
    int newKeySignature = (int8)data[0];
    //scale=data[1]?Minor:Major;
    if(keySignature != newKeySignature) {
     keySignature = newKeySignature;
     signs.insertSorted({Sign::KeySignature, track.time, .keySignature=keySignature});
    }
   }
   else if(MIDI(key)==MIDI::TrackName || MIDI(key)==MIDI::InstrumentName || MIDI(key)==MIDI::Text || MIDI(key)==MIDI::Copyright) {}
   else if(MIDI(key)==MIDI::EndOfTrack) {}
   else error("Meta", hex(key));
  }
  else error("Type", type);

  if(type==NoteOff) type=NoteOn, vel=0;
  if(type==NoteOn) {
   //log("Note", track.startTime, lastTempoChange120, track.time, lastTempoChange, lastTempoChange120 + (track.time-lastTempoChange)*120/metronome.perMinute);
   MidiNote note{lastTempoChange120 + (track.time-lastTempoChange)*120/metronome.perMinute, key, vel};
   notes.insertSorted( note );
   for(uint staff: range(2)) actives[staff].filter([key](MidiNote o){return o.key == key;}); // Releases active note
   // Commits before any repeated note on (auto release on note on without matching note off)
   for(uint staff: range(2)) { // Inserts chord now that durations are known
    for(size_t index: range(commited[staff].size)) {
     if(commited[staff][index].key !=  note.key) continue;
     MidiNote noteOn = commited[staff].take(index);

     // Value
     int duration = note.time - noteOn.time;
     if(duration) {
      const int quarterDuration = 16*metronome.perMinute/60;

      if(noteOn.time > lastOff[staff]+quarterDuration/8) { // Rest
       int duration = noteOn.time - lastOff[staff];
       assert_(duration >=/*>*/ quarterDuration/7/*4*/, duration, quarterDuration, quarterDuration/6);
       const uint quarterDuration = 16*metronome.perMinute/60;
       uint valueDuration = duration*quarterDuration/divisions;
       if(!valueDuration) valueDuration = quarterDuration/2; //FIXME
       assert_(valueDuration, duration, quarterDuration, divisions);
       bool dot=false;
       if(valueDuration%3 == 0) {
        dot = true;
        valueDuration = valueDuration * 2 / 3;
       }
       //assert_(isPowerOfTwo(valueDuration), duration, quarterDuration, divisions, valueDuration, strKey(key), "rest");
       if(valueDuration>=valueDurations[Eighth]) {
        assert_(valueDuration>=valueDurations[Eighth], duration, quarterDuration, divisions, valueDuration, strKey(0, key), "rest");
        Value value = Value(ref<uint>(valueDurations).size-1-log2(valueDuration));
        //assert_(int(value) >= 0, duration, valueDuration);
        if(int(value) >= 0) // FIXME
         signs.insertSorted({Sign::Rest, lastOff[staff], {{staff, {{duration, .rest={value}}}}}});
       }
      }

      uint valueDuration = duration*quarterDuration/divisions;
      if(!valueDuration) valueDuration = quarterDuration/2; //FIXME
      assert_(valueDuration, duration, quarterDuration, divisions);
      bool dot=false;
      uint tuplet = 1;
      if(valueDuration >= 1 && valueDuration <= 1) valueDuration = 1; // Grace
      else if(valueDuration >= 2 && valueDuration <= 2) valueDuration = 2; // Grace
      else if(valueDuration >= 3 && valueDuration <= 4) valueDuration = 4; // Semiquaver
      else if(valueDuration >= 5 && valueDuration <= 6) { // Triplet of quavers
       tuplet = 3;
       valueDuration = 8;
      }
      else if(valueDuration >= 7 && valueDuration <= 8) valueDuration = 8; // Quaver
      else if(valueDuration >= 9 && valueDuration <= 12) { // Triplet of quarter (32/3~11)
       tuplet = 3;
       valueDuration = 16;
      }
      else if(valueDuration >= 13/*14*/ && valueDuration <= 19/*16*/) valueDuration = 16; // Quarter
      else if(valueDuration >= 20/*21*/ && valueDuration <= 24) { // Dotted quarter
       dot = true;
       valueDuration = 16;
      }
      else if(valueDuration >= 25/*28*/ && valueDuration <= 35/*32*/) valueDuration = 32; // Half
      else if(valueDuration >= 36 && valueDuration <= 40) { // Half + Quaver
       // TODO: insert tied quaver before/after depending on beat
       valueDuration = 32; // FIXME: Only displays a white which is of an actual duration of a white and a quaver
      }
      else if(valueDuration >= 41/*43*/ && valueDuration <= 50/*48*/) { // Dotted white
       dot = true;
       valueDuration = 32;
      } else if(valueDuration >= 51/*60*/ && valueDuration <= 67/*64*/) { // Whole
       valueDuration = 64;
      } else if(valueDuration >= 68/*72*/ && valueDuration <= 81/*72*/) { // Whole + Quaver
       // TODO: insert tied quaver before/after depending on beat
       valueDuration = 64; // FIXME: Only displays a whole which is of an actual duration of a white and a quaver
      } else if(valueDuration >= 82/*96*/ && valueDuration <= 101/*96*/) { // Dotted Whole
       dot = true;
       valueDuration = 64;
      } else if(valueDuration>=102/*120*/ && valueDuration <= 138/*128*/) { // Double
       valueDuration = 128;
      } else if(valueDuration>=142 && valueDuration <= 149) { // Double + Quarter
       // TODO: insert tied quarter before/after depending on beat
       valueDuration = 128;// FIXME: Only displays a double which is of an actual duration of a white and a quarter
      } else if(valueDuration>=160 /*&& valueDuration <= 299*/) { // Long
       valueDuration = 256;
      }
      else error("Unsupported duration",valueDuration, duration, quarterDuration, divisions, duration*quarterDuration/divisions, strKey(0, key), dot);
      assert_(isPowerOfTwo(valueDuration), duration, quarterDuration, divisions, duration*quarterDuration/divisions, valueDuration, strKey(0, key), dot);
      Value value = Value(ref<uint>(valueDurations).size-1-log2(valueDuration));
      //assert_(int(value) >= 0, duration, valueDuration, note.time - noteOn.time);
      if(int(value) >= 0) // FIXME
       signs.insertSorted(Sign{Sign::Note, noteOn.time, {{staff, {{duration, .note={
                                                                    .value = value,
                                                                    .clef = clefs[staff],
                                                                    .step = keyStep(keySignature, key),
                                                                    .alteration = keyAlteration(keySignature, key),
                                                                    .accidental = alterationAccidental(keyAlteration(keySignature, key)), // FIXME
                                                                    .tie = Note::NoTie,
                                                                    .durationCoefficientNum = tuplet!=1 ? tuplet-1 : 1,
                                                                    .durationCoefficientDen = tuplet,
                                                                    .dot=dot,
                                                                   }}}}}});
      lastOff[staff] = note.time;
     }
     break; // Assumes single match (FIXME: assert)
    }
   }
   if(vel) { // First rough split based on pitch (final load balanced staff assignment deferred until whole chord is seen)
    uint staff = key >= 60; // C4
    for(MidiNote o: currents[staff]) assert_(o.time == note.time, o.time, note.time, "current", currents[staff]);
    //for(MidiNote o: actives[staff]) assert_(o.time == note.time, o.time, note.time, "active", actives[staff]);
    // Sorts by key (bass to treble)
    currents[staff].insertSorted( note );
    actives[staff].insertSorted( note );
   }
  }

  if(s) {
   uint8 c=s.read(); uint t=c&0x7f;
   if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|(c&0x7f);if(c&0x80){c=s.read();t=(t<<7)|c;}}}
   //if(t>=6730) { log(trackIndex, track.time, t, notes.size, type); t=0; } //FIXME
   //if(t>=3808 && t<7000) { log(trackIndex, track.time, t, notes.size, type); notes.clear(); track.startTime=track.time=lastTime=t=0; } //FIXME
   //if(t>=1680) { log(track.time, t, notes.size); t=0; }
   //else if(t>=1573 && t<7000) { log(track.time, t, notes.size); notes.clear(); track.startTime=track.time=lastTime=t=0; } // FIXME
   if(t==3944) { log(trackIndex, track.time, t, notes.size, type); notes.clear(); track.startTime=track.time=lastTime=t=0; } //FIXME
   if(t>=1090) log(track.time, t, notes.size);
   track.time += t;
  }
 }
 for(uint staff: range(2)) {
  assert_(!currents[staff], currents[staff]);
  //assert_(!actives[staff], actives[staff]);
  //assert_(!commited[staff], commited[staff]);
 }
 // Measures are signaled on end
 int measureLength = timeSignature.beats*60*divisions/metronome.perMinute;
 assert_(measureLength);
 uint nextMeasureStart = lastMeasureStart+measureLength;
 signs.insertSorted({Sign::Measure, nextMeasureStart, .measure={Measure::NoBreak, measureIndex, 1, 1, measureIndex}}); // Last measure bar
}
