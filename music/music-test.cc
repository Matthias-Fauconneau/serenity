#include "MusicXML.h"
#include "midi.h"
#include "audio.h"
#include "keyboard.h"
#include "png.h"
#include "interface.h"
#include "window.h"
#include "encoder.h"
#include "asound.h"

/// Converts MIDI time base to audio sample rate
MidiNotes scale(MidiNotes&& notes, uint targetTicksPerSeconds, int64 start) {
 assert_(notes);
 const int offset = start - (int64)notes.first().time*targetTicksPerSeconds/notes.ticksPerSeconds;
 //int offset =     - (int64)notes.first().time*targetTicksPerSeconds/notes.ticksPerSeconds;
 for(MidiNote& note: notes) {
  note.time = offset + (int64)note.time*targetTicksPerSeconds/notes.ticksPerSeconds;
 }
 notes.ticksPerSeconds = targetTicksPerSeconds;
 return move(notes);
}

uint audioStart(string audioFileName) {
 if(!audioFileName) return 0;
 for(FFmpeg file(audioFileName);;) {
  int32 buffer[1024  * file.channels];
  size_t size = file.read32(mref<int32>(buffer, 1024 * file.channels));
  for(size_t i: range(size  * file.channels)) if(abs(buffer[i])>1<<23) return file.audioTime+i;
 }
}

struct Music : Widget {
 // MusicXML
 buffer<Sign> signs;
 // PNG
 Map rawImageFileMap;
 Image image;
 array<uint> measureX; // X position in image of start of each measure
 // MIDI
 MidiNotes notes;
 buffer<Sign> midiToSign;
 array<int64> measureT;
 // MP3
 unique<FFmpeg> audioFile = nullptr;

 // View
 Scroll<ImageView> scroll;
 Keyboard keyboard; // 1/6 ~ 120

 // Highlighting
 map<uint, Sign> active; // Maps active keys to notes (indices)
 uint midiIndex = 0, noteIndex = 0;

 // Preview
 unique<Window> window = nullptr;
 Thread audioThread;
 AudioOutput audio = {{this, &Music::read32}, audioThread};

 size_t read32(mref<int2> output) {
  if(audioFile->channels == audio.channels) return audioFile->read32(mcast<int>(output));
  else if(audioFile->channels == 1 && audio.channels==2) {
   int buffer[output.size];
   audioFile->read32(mref<int>(buffer, output.size));
   for(size_t i: range(output.size)) output[i] = buffer[i];
   return output.size;
  } else error(audioFile->channels);
 }

 Music() {
  string name = arguments()[0];
  String imageFile; int2 size;
  auto list = Folder(name).list(Files);
  for(const String& file: list) {
   TextData s (file);
   if(!s.match(name)) continue;
   if(!s.match(".")) continue;
   if(!s.isInteger()) continue;
   const uint w = s.integer(false);
   if(!s.match("x")) continue;
   if(!s.isInteger()) continue;
   const uint h = s.integer(false);
   if(s) continue;
   size = int2(w, h);
   imageFile = copyRef(file);
  }
  if(!imageFile) {
   Image image = decodePNG(readFile(name+".png", Folder(name)));
   size = image.size;
   imageFile = name+"."+strx(size);
   writeFile(imageFile, cast<byte>(image), Folder(name));
  }
  rawImageFileMap = Map(imageFile, Folder(name));
  image = Image(cast<byte4>(unsafeRef(rawImageFileMap)), size);
  //Image target = copy(image);

  for(uint x: range(image.size.x)) {
   if(measureX && x<=measureX.last()+200) continue; // Minimum interval between measures
   uint sum = 0;
   for(uint y: range(image.size.y)) sum += image(x,y).g;
   if(sum < 255u*image.size.y*3/5) { // Measure Bar (2/3, 3/5, 3/4)
    //for(size_t y: range(image.size.y)) target(x,y) = 0;
    measureX.append(x);
   }
  }

  signs = MusicXML(readFile(name+".xml"_, Folder(name))).signs;

  map<uint, array<Sign>> notes;
  for(size_t signIndex: range(signs.size)) {
   Sign sign = signs[signIndex];
   if(sign.type == Sign::Note) {
    Note& note = sign.note;
    note.signIndex = signIndex;
    if(note.tie == Note::NoTie || note.tie == Note::TieStart) {
     notes.sorted(sign.time).append( sign );
    }
   }
  }

  String audioFileName = name+"/"+name+".mp3";
  audioFile = unique<FFmpeg>(audioFileName);

  this->notes = ::scale(MidiFile(readFile(name+".mid"_, Folder(name))).notes, audioFile->audioFrameRate, audioStart(audioFileName));
  buffer<MidiNote> midiNotes = filter(this->notes, [](MidiNote o){return o.velocity==0;});

  // Associates MIDI notes with score notes
  midiToSign = buffer<Sign>(midiNotes.size, 0);

  array<array<uint>> S;
  array<array<uint>> Si; // Maps sorted index to original
  for(ref<Sign> chord: notes.values) {
   array<uint> bin;
   array<uint> binI;
   for(Sign note: chord) bin.append(note.note.key());
   array<uint> binS = copyRef(bin);
   sort(binS);
   for(size_t key: binS) binI.append(bin.indexOf(key));
   S.append(move(binS));
   Si.append(move(binI));
  }
  /// Bins MIDI notes (TODO: cluster)
  array<array<uint>> M;
  array<array<uint>> Mi; // Maps original index to sorted
  for(size_t index = 0; index < midiNotes.size;) {
   array<uint> bin;
   array<uint> binI;
   int64 time = midiNotes[index].time;
    while(index < midiNotes.size && int64(midiNotes[index].time) <= time+1302) { // TODO: cluster size with most similar bin count/size
    bin.append(midiNotes[index].key);
    index++;
   }
   array<uint> binS = copyRef(bin);
   sort(binS);
   for(size_t key: bin) binI.append(binS.indexOf(key));
   M.append(move(binS));
   Mi.append(move(binI));
  }

  /// Synchronizes MIDI and score using dynamic time warping
  size_t m = S.size, n = M.size;
  assert_(m <= n, m, n);

  // Evaluates cumulative score matrix at each alignment point (i, j)
  struct Matrix {
   size_t m, n;
   buffer<float> elements;
   Matrix(size_t m, size_t n) : m(m), n(n), elements(m*n) { elements.clear(0); }
   float& operator()(size_t i, size_t j) { return elements[i*n+j]; }
  } D(m,n);
  // Reversed scan here to have the forward scan when walking back the best path
  for(size_t i: reverse_range(m)) for(size_t j: reverse_range(n)) { // Evaluates match (i,j)
   float d = 0;
   for(uint s: S[i]) for(uint m: M[j]) d += s==m;
   // Evaluates best cumulative score to an alignment point (i,j)
   D(i,j) = max(max(
                 j+1==n?0:D(i,j+1), // Ignores peak j
                 i+1==m?0:D(i+1, j) ), // Ignores chord i
                ((i+1==m||j+1==n)?0:D(i+1,j+1)) + d ); // Matches chord i with peak j
  };

  // Evaluates _strictly_ monotonous map by walking back the best path on the cumulative score matrix
  // Forward scan (chronologic)
  size_t i = 0, j = 0; // Score and MIDI bins indices
  size_t signIndex = 0;
  while(i<m && j<n) {
   /**/ if(i+1<m && D(i,j) == D(i+1,j)) {
    i++;
   }
   else if(j+1<n && D(i,j) == D(i,j+1)) {
    for(size_t unused k: range(M[j].size)) midiToSign.append(Sign{});
    j++;
   } else {
    for(size_t k: range(M[j].size)) {
     Sign sign{};
     if(Mi[j][k]<notes.values[i].size) sign = notes.values[i][Si[i][Mi[j][k]]]; // Map original MIDI index to sorted to original note index
     size_t midiIndex = midiToSign.size;
     midiToSign.append( sign );
     if(sign.note.signIndex != invalid) {
      size_t nextSignIndex = sign.note.signIndex;
      for(;signIndex < nextSignIndex;signIndex++) {
       Sign sign = signs[signIndex];
       if(sign.type == Sign::Measure) measureT.append(midiNotes[midiIndex].time);
      }
     }
    }
    i++; j++;
   }
  }
  for(;j<n;j++) for(size_t unused k: range(M[j].size)) midiToSign.append(Sign{});

  assert_(midiToSign.size == midiNotes.size, midiNotes.size, midiToSign.size);
  assert_(measureT.size <= measureX.size-1, measureT.size, measureX.size);
  scroll.image = unsafeRef(image);
  scroll.horizontal=true, scroll.vertical=false, scroll.scrollbar = true;
  window = ::window(this);

  audio.start(audioFile->audioFrameRate, 1024, 32, 2);
  audioThread.spawn();
 }

 bool follow(int64 timeNum, int64 timeDen, vec2 size) {
  //constexpr int staffCount = 2;
  bool contentChanged = false;
  for(;midiIndex < notes.size && (int64)notes[midiIndex].time*timeDen <= timeNum*(int64)notes.ticksPerSeconds; midiIndex++) {
   MidiNote note = notes[midiIndex];
   if(note.velocity) {
    //assert_(noteIndex < sheet.midiToSign.size, noteIndex, sheet.midiToSign.size);
    Sign sign = midiToSign[noteIndex];
    if(sign.type == Sign::Note) {
     active.insertMulti(note.key, sign);
     (sign.staff?keyboard.left:keyboard.right).append( sign.note.key() );
     /*if(sign.note.pageIndex != invalid && sign.note.glyphIndex[0] != invalid) {
      assert_(sign.note.pageIndex == 0);
      for(size_t index: ref<size_t>(sign.note.glyphIndex)) if(index!=invalid) system.glyphs[index].color = (sign.staff?red:(staffCount==1?blue:green));
      contentChanged = true;
     }*/
    }
    // Updates next notes
#if 0
    size_t firstSignIndex = sign.note.signIndex;
    assert_(firstSignIndex != invalid);
    //bool firstNote = true;
    for(size_t signIndex: range(firstSignIndex, signs.size)) {
     assert_(signIndex < signs.size, signIndex);
     const Sign& sign = signs[signIndex];
     if(sign.type == Sign::Note) {
      //keyboard.measure.insertMulti(sign.note.key(), sign);
      //if(!firstNote && sign.note.finger) break; // Shows all notes until next hand position change
      //firstNote = false;
     }
     if(sign.type == Sign::Measure) break;
    }
#endif
    noteIndex++;
   }
   else if(!note.velocity && active.contains(note.key)) {
    while(active.contains(note.key)) {
     Sign sign = active.take(note.key);
     //fret.active.take(note.key);
     //if(fret.measure.contains(note.key)) fret.measure.remove(note.key);
     (sign.staff?keyboard.left:keyboard.right).remove( sign.note.key() );
     /*if(sign.note.pageIndex != invalid && sign.note.glyphIndex[0] != invalid) {
      assert_(sign.note.pageIndex == 0);
      for(size_t index: ref<size_t>(sign.note.glyphIndex)) if(index!=invalid)  system.glyphs[index].color = black;
     }*/
#if 0
     // Updates next notes
     //keyboard.measure.clear();
     size_t firstSignIndex = sign.note.signIndex+1;
     assert_(firstSignIndex != invalid);
     //bool firstNote = true;
     for(size_t signIndex: range(firstSignIndex, signs.size)) {
      assert_(signIndex < signs.size, signIndex);
      const Sign& sign = signs[signIndex];
      if(sign.type == Sign::Note) {
       //keyboard.measure.insertMulti(sign.note.key(), sign);
       //if(!firstNote && sign.note.finger) break; // Shows all notes until next hand position change
       //firstNote = false;
      }
      if(sign.type == Sign::Measure) break;
     }
#endif
     contentChanged = true;
    }
   }
  }

  int64 t = (int64)timeNum*notes.ticksPerSeconds;
  float previousOffset = scroll.offset.x;
  // Cardinal cubic B-Spline
  for(int index: range(measureT.size-1)) {
   int64 t1 = (int64)measureT[index]*timeDen;
   int64 t2 = (int64)measureT[index+1]*timeDen;
   if(t1 <= t && t < t2) {
    double f = double(t-t1)/double(t2-t1);
    double w[4] = { 1./6 * cb(1-f), 2./3 - 1./2 * sq(f)*(2-f), 2./3 - 1./2 * sq(1-f)*(2-(1-f)), 1./6 * cb(f) };
    auto X = [&](int index) { return clamp(0.f, measureX[clamp<int>(0, index, measureX.size-1)] - size.x/2, image.size.x-size.x); };
    float newOffset = round( w[0]*X(index-1) + w[1]*X(index) + w[2]*X(index+1) + w[3]*X(index+2) );
    if(newOffset >= -scroll.offset.x) scroll.offset.x = -newOffset;
    break;
   }
  }
  if(previousOffset != scroll.offset.x) contentChanged = true;
  //synchronizer.currentTime = (int64)timeNum*notes.ticksPerSeconds/timeDen;
#if 0
  if(video) {
   while((int64)video.videoTime*timeDen < (int64)timeNum*video.timeDen) {
    Image image = video.read();
    if(!image) { if(!preview) log("Missing image"); break; }
    assert_(image);
    videoView.image = ::move(image);
    contentChanged=true;
    // Only preview may have lower framerate than video
    assert_((int64)video.videoTime*timeDen >= (int64)timeNum*video.timeDen || preview || video.videoTime == 0 /*First frame might have a negative timecode*/, video.videoTime, video.timeDen, timeNum, timeDen);
   }
  }
#endif
  return contentChanged;
 }

 vec2 sizeHint(vec2) override { return vec2(1366, 435); }
 shared<Graphics> graphics(vec2 size) override {
  follow(audioFile->audioTime, audioFile->audioFrameRate, vec2(window->size));
  window->render();
  return scroll.ScrollArea::graphics(size);
 }
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) override {
return scroll.ScrollArea::mouseEvent(cursor, size, event, button, focus);
 }
 bool keyPress(Key key, Modifiers modifiers) override { return scroll.ScrollArea::keyPress(key, modifiers); }
} test;
