#include "midi.h"
#include "file.h"
#include "math.h"
inline uint log2(uint v) { uint r=0; while(v >>= 1) r++; return r; }

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
 ticksPerBeat = (uint16)s.read(); // Defaults to 60 qpm (FIXME: max 64/metronome.perMinute)
 notes.ticksPerSeconds = 120/60*ticksPerBeat; // Ticks per second (Default tempo is 120bpm/60s/min)

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
 uint64 usPerBeat = 60000000 / 120; // 500000
 Metronome metronome = {Quarter, 120};
 uint lastTempoChange = 0, lastTempoChangeUS = 0; // Last tempo changes in ticks and microseconds
 uint measureIndex = 0;
 int lastMeasureStart = 0;
 constexpr int staffCount = 1;
 Clef clefs[staffCount];
 if(staffCount==1) clefs[0] = {GClef,0};
 else { clefs[0] = {FClef,0}, clefs[1] = {GClef,0}; }
 for(uint staff: range(staffCount)) signs.insertSorted({Sign::Clef, 0, {{staff, {.clef=clefs[staff]}}}});
 array<MidiNote> currents[staffCount]; // New notes to be pressed
 array<MidiNote> actives[staffCount]; // Notes currently pressed
 array<MidiNote> commited[staffCount]; // Commited/assigned notes to be written once duration is known (on note off)
 uint lastOff[staffCount] = {}; // For rests
 signs.insertSorted({Sign::TimeSignature, 0, .timeSignature=timeSignature});
 for(uint lastTime = 0;;) {
  size_t trackIndex = invalid;
  for(size_t index: range(tracks.size))
   if(tracks[index].data && (trackIndex==invalid || tracks[index].time < tracks[trackIndex].time))
    trackIndex = index;
  if(trackIndex == invalid) break;
  Track& track = tracks[trackIndex];
  assert_(track.time >= lastTime);

  if(track.time != lastTime) { // Commits last chord
   uint sustain[staffCount];
   for(uint i: range(staffCount)) sustain[i] = (uint)actives[i].size; // Remaining notes kept sustained
   // Balances load on both hand
   for(size_t staff: range(staffCount)) {
    array<MidiNote>& active = actives[staff];
    array<MidiNote>& otherActive = actives[!staff];
    array<MidiNote>& current = currents[staff];
    array<MidiNote>& other = currents[!staff];
    if(0) {
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
   }
   for(size_t staff: range(staffCount)) commited[staff].append(::move(currents[staff]));  // Defers until duration is known (on note off)
   for(size_t staff: range(staffCount)) currents[staff].clear(); // FIXME: ^ move should already clear currents[staff]
   for(size_t staff: range(staffCount)) assert_(!currents[staff], currents, commited);
  }

  lastTime = track.time;
  assert_(track.time >= lastTempoChange);
  uint64 trackTimeUS = lastTempoChangeUS + (track.time-lastTempoChange)*usPerBeat/ticksPerBeat;

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
   uint8 c=s.read(); uint len=c&0x7f; if(c&0x80){ assert_(!(c&0x80)); c=s.read(); len=(len<<7)|c; }
   enum class MIDI { SequenceNumber, Text, Copyright, TrackName, InstrumentName, Lyrics, Marker, Cue, ChannelPrefix=0x20,
                     EndOfTrack=0x2F, Tempo=0x51, Offset=0x54, TimeSignature=0x58, KeySignature, SequencerSpecific=0x7F };
   ref<uint8> data = s.read<uint8>(len);
   if(MIDI(key)==MIDI::TimeSignature) {
    uint beats = data[0];
    uint beatUnit = 1<<data[1];
    TimeSignature newTimeSignature {beats, beatUnit};
    if(newTimeSignature.beats != timeSignature.beats || newTimeSignature.beatUnit != timeSignature.beatUnit) {
     signs.insertSorted({Sign::TimeSignature, track.time, .timeSignature=timeSignature});
     assert_(track.time == 0, track.time);
    }
   }
   else if(MIDI(key)==MIDI::Tempo) {
    lastTempoChangeUS += (track.time - lastTempoChange) * usPerBeat/ticksPerBeat;
    lastTempoChange = track.time;
    usPerBeat = ((data[0]<<16)|(data[1]<<8)|data[2]); // Microseconds per beat (quarter)
    trackTimeUS = lastTempoChangeUS;
    metronome.perMinute = 60000000 / usPerBeat; // Beats per minute
    //if(perMinute) signs.insertSorted({track.time, 0, uint(-1), Sign::Metronome, .metronome={Quarter, perMinute}});
   }
   else if(MIDI(key)==MIDI::KeySignature) {
    int newKeySignature = (int8)data[0];
    //scale=data[1]?Minor:Major;
    if(keySignature != newKeySignature) {
     keySignature = newKeySignature;
     signs.insertSorted({Sign::KeySignature, trackTimeUS, .keySignature=keySignature});
    }
   }
   else if(MIDI(key)==MIDI::TrackName || MIDI(key)==MIDI::InstrumentName || MIDI(key)==MIDI::Text || MIDI(key)==MIDI::Copyright) {}
   else if(MIDI(key)==MIDI::EndOfTrack) {}
   else error("Meta", hex(key));
  }
  else error("Type", type);

  if(type==NoteOff) type=NoteOn, vel=0;
  if(type==NoteOn) {
   MidiNote note{trackTimeUS, key, vel};
   if(note.velocity) {
    if(notes && notes.last().velocity && notes.last().time <= note.time)
     assert_(notes.last().key != key, notes.last(), note, notes.last().velocity, note.velocity, notes.last().time, note.time);
    for(MidiNote o: notes) if(o.velocity) {
     if(o.key == key && note.time == o.time) goto continue2_;
     assert_(o.key != key || abs(o.time-note.time), o, note, int(note.time-o.time), o.velocity, note.velocity);
    }
   }
   notes.insertSorted( note );
   for(uint staff: range(staffCount)) actives[staff].filter([key](MidiNote o){return o.key == key;}); // Releases active note
   // Commits before any repeated note on (auto release on note on without matching note off)
   for(uint staff: range(staffCount)) { // Inserts chord now that durations are known
    for(size_t index: range(commited[staff].size)) {
     if(commited[staff][index].key !=  note.key) continue;
     MidiNote noteOn = commited[staff].take(index);

     const int measureLengthUS = timeSignature.beats*usPerBeat;
     assert_(measureLengthUS);

     // Value
     int duration = note.time - noteOn.time; // us
     if(duration) {
      int64 totalDuration = noteOn.time - lastOff[staff]; // us
      int64 restStart = lastOff[staff]; // us
      while(totalDuration >= int64(usPerBeat/4)) {
       int64 nextMeasureStart = lastMeasureStart+measureLengthUS;
       int64 restDuration = ::min(totalDuration, nextMeasureStart-restStart);
       if(restDuration*16/usPerBeat>=8) {
        //log(restDuration*16/usPerBeat);
        signs.insertSorted({Sign::Rest, uint64(restStart), {{staff, {{restDuration, .rest={Value(ref<uint>(valueDurations).size-1-log2(restDuration*16/usPerBeat))}}}}}});
       //log(restStart, restDuration, restStart+restDuration);
       }
       totalDuration -= restDuration;
       restStart += restDuration;
       if(restStart >= nextMeasureStart) {
        lastMeasureStart = nextMeasureStart;
        signs.insertSorted({Sign::Measure, uint(nextMeasureStart), .measure={Measure::NoBreak, measureIndex, 1, 1, measureIndex}});
        measureIndex++;
       }
      }

      // When latest track is ready to switch measure
      uint nextMeasureStart = lastMeasureStart+measureLengthUS;
      if(trackTimeUS >= nextMeasureStart) {
       lastMeasureStart = nextMeasureStart;
       signs.insertSorted({Sign::Measure, nextMeasureStart, .measure={Measure::NoBreak, measureIndex, 1, 1, measureIndex}});
       measureIndex++;
      }

      uint valueDuration = duration*16/usPerBeat;
      if(!valueDuration) valueDuration = 8; //FIXME
      assert_(valueDuration, duration, ticksPerBeat);
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
      } else if(valueDuration>=102/*120*/ && valueDuration <= 141/*128*/) { // Double
       valueDuration = 128;
      } else if(valueDuration>=142 && valueDuration <= 149) { // Double + Quarter
       // TODO: insert tied quarter before/after depending on beat
       valueDuration = 128;// FIXME: Only displays a double which is of an actual duration of a white and a quarter
      } else if(valueDuration>=150 /*&& valueDuration <= 299*/) { // Long
       valueDuration = 256;
      }
      else error("Unsupported duration", valueDuration, duration, ticksPerBeat, duration*32/ticksPerBeat, strKey(0, key), dot);
      assert_(isPowerOfTwo(valueDuration), duration, ticksPerBeat, duration*32/ticksPerBeat, valueDuration, strKey(0, key), dot);
      Value value = Value(ref<uint>(valueDurations).size-1-log2(valueDuration));
      //assert_(int(value) >= 0, duration, valueDuration, note.time - noteOn.time);
      if(int(value) >= 0) // FIXME
       signs.insertSorted(Sign{Sign::Note, noteOn.time, {{staff, {{duration, .note={
                                                                    .value = value,
                                                                    .clef = clefs[staff],
                                                                    .step = keyStep(keySignature, key),
                                                                    .alteration = keyAlteration(keySignature, key),
                                                                    //.accidental = alterationAccidental(keyAlteration(keySignature, key)), // FIXME
                                                                    .accidental = keyAlteration(keySignature, key) ? alterationAccidental(keyAlteration(keySignature, key)) : Accidental::None, // FIXME
                                                                    .tie = Note::NoTie,
                                                                    .durationCoefficientNum = tuplet!=1 ? tuplet-1 : 1,
                                                                    .durationCoefficientDen = tuplet,
                                                                    .dot=dot,
                                                                   }}}}}});
      //log("on", noteOn.time, noteOn.time*32/ticksPerBeat, "off", note.time, note.time*32/ticksPerBeat);
      lastOff[staff] = note.time;
     }
     break; // Assumes single match (FIXME: assert)
    }
   }
   if(vel) { // First rough split based on pitch (final load balanced staff assignment deferred until whole chord is seen)
    uint staff = key >= 60; // C4
    staff = ::min(staff, uint(staffCount)-1);
    for(MidiNote o: currents[staff]) assert_(o.time == note.time, o.time, note.time, "current", currents[staff]);
    //for(MidiNote o: actives[staff]) assert_(o.time == note.time, o.time, note.time, "active", actives[staff]);
    // Sorts by key (bass to treble)
    currents[staff].insertSorted( note );
    actives[staff].insertSorted( note );
   }
  }
  continue2_:;

  if(s) {
   uint8 c = s.read(); uint t = c&0x7F;
   if(c&0x80) { c = s.read(); t=(t<<7)|(c&0x7F);
    if(c&0x80) { c=s.read(); t=(t<<7)|(c&0x7F);
     if(c&0x80) { c=s.read();  t=(t<<7)|c;
     assert_(!(c&0x80));
     }
    }
   }
   track.time += t;
  }
 }
 for(uint staff: range(staffCount)) {
  assert_(!currents[staff], currents[staff]);
  //assert_(!actives[staff], actives[staff]);
  //assert_(!commited[staff], commited[staff]);
 }
 // Measures are signaled on end
 const int measureLength = timeSignature.beats*ticksPerBeat/2; //t/q = t/s * 60s/m / (120q/m)
 assert_(measureLength);
 uint nextMeasureStart = lastMeasureStart+measureLength;
 signs.insertSorted({Sign::Measure, nextMeasureStart, .measure={Measure::NoBreak, measureIndex, 1, 1, measureIndex}}); // Last measure bar
}
