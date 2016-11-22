#include "MusicXML.h"
#include "midi.h"
#include "audio.h"
#include "png.h"
#include "interface.h"
#include "window.h"
#include "encoder.h"

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

struct MusicTest {
 Map rawImageFileMap;
 Image image, target;
 ImageView view;
 unique<Window> window = nullptr;
 MusicTest() {
  string name = arguments()[0];
  String imageFile; int2 size;
  auto list = currentWorkingDirectory().list(Files);
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
   Image image = decodePNG(readFile(name+".png"));
   size = image.size;
   imageFile = name+"."+strx(size);
   writeFile(imageFile, cast<byte>(image));
  }
  rawImageFileMap = Map(imageFile);
  image = Image(cast<byte4>(unsafeRef(rawImageFileMap)), size);
  target = copy(image);
  view = cropRef(target,0,int2(1366*2,870));
  window = ::window(&view);
  array<uint> measureX; // X position in image of start of each measure
  for(uint x: range(image.size.x)) {
   uint sum = 0;
   for(uint y: range(image.size.y)) sum += image(x,y).g;
   if(sum < 255u*image.size.y*2/3) { // Measure Bar
    for(size_t y: range(image.size.y)) target(x,y) = 0;
    measureX.append(x);
   }
  }
  MusicXML xml = readFile(name+".xml"_);

  const mref<Sign> signs = xml.signs;

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

  String audioFileName = name+".mp3";
  FFmpeg audioFile = FFmpeg(audioFileName);

  MidiNotes midi = ::scale(MidiFile(readFile(name+".mid"_)).notes, audioFile.audioFrameRate, audioStart(audioFileName));
  buffer<MidiNote> midiNotes = filter(midi, [](MidiNote o){return o.velocity==0;});

  // Associates MIDI notes with score notes
  buffer<Sign> midiToSign = buffer<Sign>(midiNotes.size, 0);

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
   //if(index < midiNotes.size) log(int64(midiNotes[index].time) - time, strKey(-4, midiNotes[index].key));
   array<uint> binS = copyRef(bin);
   sort(binS);
   for(size_t key: bin) binI.append(binS.indexOf(key));
   //log(apply(S[min(M.size, S.size-1)], [](uint key){return strKey(-4, key);}),":", apply(binS, [](uint key){return strKey(-4, key);}));
   M.append(move(binS));
   Mi.append(move(binI));
  }

  /// Synchronizes MIDI and score using dynamic time warping
  size_t m = S.size, n = M.size;
  //assert_(m <= n, m, n);
  //log(m, n);

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
  while(i<m && j<n) {
   /**/ if(i+1<m && D(i,j) == D(i+1,j)) {
    i++;
   }
   else if(j+1<n && D(i,j) == D(i,j+1)) {
    for(size_t unused k: range(M[j].size)) midiToSign.append(Sign{});
    j++;
   } else {
    log(apply(S[i], [](uint key){return strKey(-4, key);}),":", apply(M[j], [](uint key){return strKey(-4, key);}));
    for(size_t k: range(M[j].size)) {
     //assert_(i < notes.values.size && k < notes.values[i].size, i, k, notes.values.size);
     Sign sign{};
     if(Mi[j][k]<notes.values[i].size) sign = notes.values[i][Si[i][Mi[j][k]]]; // Map original MIDI index to sorted to original note index
     //assert_(sign.note.signIndex != invalid);
     midiToSign.append( sign );
    }
    //for(size_t unused k: range(S[i].size, M[j].size)) midiToSign.append(Sign{});
    i++; j++;
   }
  }
  for(;j<n;j++) for(size_t unused k: range(M[j].size)) midiToSign.append(Sign{});
  assert_(midiToSign.size == midiNotes.size, midiNotes.size, midiToSign.size);
 }
} test;
