/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "midi-input.h"
#include "sampler.h"
#include "asound.h"
#if UI
#include "window.h"
#include "rle.h"
#include "MusicXML.h"
#include "midi.h"
#include "png.h"
#include "sort.h"
#include "render.h"

Image8 toImage8(const Image& image) {
 return Image8(apply(image, [](byte4 bgr){
                return (uint8)((bgr.b+bgr.g+bgr.r)/3); // FIXME: coefficients ?
               }), image.size);
}
#endif

/// SFZ sampler and PDF renderer
struct Music
  #if UI
  : Widget
  #endif
{
 const Folder folder = "."_;
 array<String> files = folder.list(Files|Sorted);
 String title;

 Thread decodeThread {19};
 unique<Sampler> sampler = nullptr;

#if UI
 //array<unique<FontData>> fonts;
 //unique<Sheet> sheet = nullptr;
 //unique<Scroll<HList<GraphicsWidget>>> pages;
 Image8 image;
 unique<Window> window = nullptr;

 buffer<Sign> signs;
 uint ticksPerSeconds = 0;
 array<uint> measureX; // X position in image of start of each measure
 struct OCRNote {
  int confidence;
  int value; // 0: Quarter, 1: Half, 2: Whole
  int2 position;
  bgr3f color;
  size_t glyphIndex;
  bool operator <(const OCRNote& b) const { return position.y < b.position.y; }
  bool operator ==(size_t glyphIndex) const { return this->glyphIndex == glyphIndex; }
 };
 array<OCRNote> OCRNotes;
 // MIDI
 MidiNotes notes;
 buffer<Sign> midiToSign;
 array<int64> measureT;
 // View
 //map<uint, Sign> active; // Maps active keys to notes
 map<uint, Sign> expected; // Maps expected keys to notes
 uint midiIndex = 0, noteIndex = 0;
 buffer<Image8> templates;
 array<OCRNote> highlight;
 float offset = 0;

 Thread audioThread{-20};
 AudioOutput audio {/*window?*/audioThread/*:mainThread*/};
 MidiInput input {/*window?*/audioThread/*:mainThread*/};
#else
 AudioOutput audio {mainThread};
 MidiInput input {mainThread};
#endif

 Music() {
#if UI
  if(files) setTitle(arguments() ? arguments()[0] : files[0]);
  window = ::window(/*&pages->area()*/this, int2(0, 0/*image.size.x*/));
  if(window) {
   //window->actions[Key('s')] = [this]{ writeFile("keyboard", str(input.max), Folder(".config"_, home()), true); };
   window->actions[UpArrow] = {this, &Music::previousTitle};
   window->actions[DownArrow] = [this]{ writeFile("keyboard", str(input.max), Folder(".config"_, home()), true); nextTitle(); }; //{this, &Music::nextTitle};
   window->actions[Return] = {this, &Music::nextTitle};
  }
#endif
  setInstrument("Piano");

  //AudioControl("Master Playback Switch") = 1;
  //AudioControl("Headphone Playback Switch") = 1;
  //AudioControl("Master Playback Volume") = 100;
  audio.start(sampler->rate, sampler->periodSize, 32, 2, true);
  //sampler->noteEvent(60, 64);
 }

 void setInstrument(string name) {
#if UI
  if(audioThread) audioThread.wait();
#endif
  if(decodeThread) decodeThread.wait(); // ~Thread
  sampler = unique<Sampler>(name+'/'+name+".autocorrected.sfz"_, 256, [this](uint){ input.event(); }, decodeThread); // Ensures all events are received right before mixing
  input.noteEvent = {sampler.pointer, &Sampler::noteEvent};
  input.ccEvent = {sampler.pointer, &Sampler::ccEvent};
  //input.noteEvent = {this, &Music::noteEvent};
  audio.read32 = {sampler.pointer, &Sampler::read32};
  //audioThread.spawn();
  decodeThread.spawn();
 }

#if UI
 void setTitle(string title) {
  //assert_(window);
  if(endsWith(title,".pdf"_)||endsWith(title,".xml"_)||endsWith(title,".mid"_)) title=title.slice(0,title.size-4);
  this->title = copyRef(title);
#if 0
  buffer<Graphics> pages;
  /**/ if(existsFile(title+".xml"_, folder)) { // MusicXML
   MusicXML musicXML (readFile(title+".xml"_, folder));
   sheet = unique<Sheet>(musicXML.signs, musicXML.divisions, window->size);
   pages = move(sheet->pages);
  }
  else if(existsFile(title+".pdf"_, folder)) { // PDF
   //pages = decodePDF(readFile(title+".pdf"_, folder), fonts);
  }
  else if(existsFile(title+".mid"_, folder)) { // MIDI
   MidiFile midi (readFile(title+".mid"_, folder));
   sheet = unique<Sheet>(midi.signs, midi.ticksPerBeat, window->size);
   pages = move(sheet->pages);
  }
  else
#endif
  {
   Time time{true};
   string name = title;
   String imageFile; int2 size;
   auto list = folder.list(Files);
   for(const String& file: list) {
    TextData s (file);
    if(!s.match(name)) continue;
    if(!s.match(".")) continue;
    if(!s.isInteger()) continue;
    const uint w = s.integer(false);
    if(!s.match("x")) continue;
    if(!s.isInteger()) continue;
    const uint h = s.integer(false);
    if(!s.match(".rle")) continue;
    if(s) continue;
    size = int2(w, h);
    imageFile = copyRef(file);
   }
   if(!imageFile) return;
   image = Image8(decodeRunLength(cast<uint8>(readFile(imageFile))), int2(size.y, size.x));
   log(time, size, float(image.ref::size)/1024/1024,"M");
   //for(int x: range(image.size.x)) for(int y: range(image.size.y)) image[y*image.stride+x] = transpose[x*image.size.y+y];
   //log(time);
   measureX = cast<uint>(readFile("measureX", name));
   OCRNotes = cast<OCRNote>(readFile("OCRNotes", name));
   MusicXML musicXML(readFile(name+".xml"_, Folder(name)));
   signs = ::move(musicXML.signs);
   {
    ticksPerSeconds = 0;
    for(Sign sign: signs) {
     if(sign.type==Sign::Metronome) {
      assert_(!ticksPerSeconds || ticksPerSeconds == sign.metronome.perMinute*musicXML.divisions, ticksPerSeconds, sign.metronome.perMinute*musicXML.divisions, sign.metronome.perMinute, musicXML.divisions);
      ticksPerSeconds = sign.metronome.perMinute*musicXML.divisions;
     }
    }
    if(!ticksPerSeconds) ticksPerSeconds = 90*musicXML.divisions;
   }

   map<uint, array<Sign>> notes; // skips tied (for MIDI)
   map<uint, array<Sign>> allNotes; // also tied (for OCR)
   for(size_t signIndex: range(signs.size)) {
    Sign sign = signs[signIndex];
    if(sign.type == Sign::Note) {
     Note& note = sign.note;
     note.signIndex = signIndex;
     allNotes.sorted(sign.time).add( sign );
     if(note.tie == Note::NoTie || note.tie == Note::TieStart) {
      notes.sorted(sign.time).add( sign );
     }
    }
   }

   array<OCRNote> sorted; // Bins close X together
   array<OCRNote> bin; int lastX = 0;
   templates = apply(3, [name](int i){ return toImage8(decodePNG(readFile(str(i)+".png", Folder(name))));});
   assert_(templates.size==3);
   for(OCRNote note: OCRNotes) {
    if(note.position.x > lastX+templates[2].size.x) {
     sorted.append(bin);
     bin.clear();
     lastX = note.position.x;
    }
    bin.insertSorted(note); // Top to Bottom
   }
   sorted.append(bin);
   OCRNotes = ::move(sorted);

   uint glyphIndex = 0; // X (Time), Top to Bottom
   for(ref<Sign> chord: allNotes.values) {
    for(Sign note: chord.reverse()) { // Top to Bottom
     // Transfers to notes
     for(mref<Sign> chord: notes.values) for(Sign& o: chord) {
      if(o.note.signIndex == note.note.signIndex) o.note.glyphIndex[0] = glyphIndex; // FIXME: dot, accidentals
      if(o.note.signIndex == (size_t)note.note.tieStartNoteIndex) o.note.glyphIndex[1] = glyphIndex; // Also highlights tied notes
     }
     glyphIndex++;
    }
   }

   if(glyphIndex != OCRNotes.size) log(glyphIndex, OCRNotes.size);
   assert_(glyphIndex <= OCRNotes.size, glyphIndex, OCRNotes.size);

   this->notes = MidiFile(readFile(name+".mid"_, Folder(name))).notes; //::scale(MidiFile(readFile(name+".mid"_, Folder(name))).notes, 48000, 0);
   buffer<MidiNote> midiNotes = filter(this->notes, [](MidiNote o){return o.velocity==0;});

   // Associates MIDI notes with score notes
   midiToSign = buffer<Sign>(midiNotes.size, 0);

   array<array<uint>> S;
   array<array<uint>> Si; // Maps sorted index to original
   uint64 minT=-1; {
    uint lastTime = 0;
    array<uint> bin;
    array<uint> binI;
    for(ref<Sign> chord: notes.values) {
     for(Sign note: chord) {
      //log(note.time-lastTime);
      if(note.time-lastTime > 0/*84*/) {
       minT = ::min(minT, note.time-lastTime);
       array<uint> binS = copyRef(bin);
       sort(binS); // FIXME: already sorted ?
       for(size_t key: binS) binI.append(bin.indexOf(key));
       bin.clear();
       S.append(move(binS));
       Si.append(move(binI));
      }
      lastTime = note.time;
      bin.add(note.note.key());
     }
    }
    //log(minT);
   }
   /// Bins MIDI notes (TODO: cluster)
   array<array<uint>> M;
   array<array<uint>> Mi; // Maps original index to sorted
   for(size_t index = 0; index < midiNotes.size;) {
    array<uint> bin;
    array<uint> binT;
    array<uint> binI;
    int64 time = midiNotes[index].time;
    while(index < midiNotes.size && (int64(midiNotes[index].time) < time+993/*1779*/ ||
                                    (int64(midiNotes[index].time) <= time+16985 && S[::min(M.size, S.size-1)].contains(midiNotes[index].key)
                                      && bin.size < S[::min(M.size, S.size-1)].size)) ) { //FIXME: merge bins at sync time
     bin.append(midiNotes[index].key); // add
     binT.append(midiNotes[index].time);
     index++;
    }
    array<uint> binS = copyRef(bin);
    sort(binS);
    for(size_t key: bin) binI.append(binS.indexOf(key));
    Mi.append(move(binI));
    M.append(move(binS));
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
    for(uint s: S[i]) for(uint m: M[j]) d += s%12==m%12; // No octave penalty
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
   measureT.append(0);
   while(i<m && j<n) {
    /**/ if(i+1<m && D(i,j) == D(i+1,j)) { // in Score not in MID
     i++;
    }
    else if(j+1<n && D(i,j) == D(i,j+1)) {
     for(size_t unused k: range(M[j].size)) midiToSign.append(Sign{});
     j++;
    } else {
     for(size_t k: range(M[j].size)) {
      size_t midiIndex = midiToSign.size;

      Sign sign{};
      if(Mi[j][k]<notes.values[i].size) sign = notes.values[i][Si[i][Mi[j][k]]]; // Maps original MIDI index to sorted to original note index
      else log("Missing note", strKey(-1, midiNotes[midiIndex].key));
      midiToSign.append( sign );
      if(sign.note.signIndex != invalid) {
       size_t nextSignIndex = sign.note.signIndex;
       for(;signIndex < nextSignIndex;signIndex++) {
        Sign sign = signs[signIndex];
        if(sign.type == Sign::Measure) {
         measureT.append(midiNotes[midiIndex].time);
        }
       }
      }
     }
     i++; j++;
    }
   }
   assert_(j == n);
   measureT.append(this->notes.last().time);
  }
  //else error(title);
  //this->pages = unique<Scroll<HList<GraphicsWidget>>>( apply(pages, [](Graphics& o) { return GraphicsWidget(move(o)); }) );
  //this->pages->vertical = false;
  //this->pages->horizontal = true;
  //window->widget = window->focus = &this->pages->area();
  expect();
  if(window) { window->render(); window->setTitle(title); }
 }

 void expect() {
  while(!expected && noteIndex<midiToSign.size) {
   Sign first = midiToSign[noteIndex];
   for(;midiToSign[noteIndex].time == first.time; noteIndex++) {
    const Sign sign = midiToSign[noteIndex];
    const uint key = sign.note.key();
    if(!expected.contains(key)) {
     bool hasHighlight = false;
     for(int i: range(2)) {
      if(sign.note.glyphIndex[i] != invalid) {
       OCRNote note = OCRNotes[sign.note.glyphIndex[i]];
       note.glyphIndex = sign.note.glyphIndex[i]; // For removal
       note.color = (sign.staff?red:green);
       highlight.append(note);
       hasHighlight = true;
      }
     }
     if(hasHighlight) expected.insert(key, sign);
    }
   }
  }
  if(window) window->setTitle(str(apply(expected.keys, [](uint key){return strKey(0, key);}), highlight.size));
  assert_(highlight);
 }

 void noteEvent(uint key, uint velocity) {
  bool contentChanged = false;
  if(velocity) {
   for(;;) { // Match all harmonics with a single note
    Sign sign{};
    if(expected.contains(key)) sign = expected.take(key);
    else if(expected.contains(key+12)) sign = expected.take(key+12);
    else if(expected.contains(key-12)) sign = expected.take(key-12);
    else break;
    for(int i: range(2)) if(sign.note.glyphIndex[i] != invalid) highlight.remove(sign.note.glyphIndex[i]);
    contentChanged = true;
    //expected.clear(); // Match one note to continue
    while(expected) {
     Sign sign = expected.values.pop(); expected.keys.pop();
     for(int i: range(2)) if(sign.note.glyphIndex[i] != invalid) highlight.remove(sign.note.glyphIndex[i]);
    }
   }
   expect();
  }
  if(!expected) return;

  const int64 t = (int64)expected.values.last().time*(int64)notes.ticksPerSeconds;
  float previousOffset = offset;
  // Cardinal cubic B-Spline
  for(int index: range(measureT.size-1)) {
   int64 t1 = (int64)measureT[index]*(int64)ticksPerSeconds;
   int64 t2 = (int64)measureT[index+1]*(int64)ticksPerSeconds;
   if(t1 <= t && t < t2) {
    double f = double(t-t1)/double(t2-t1);
    double w[4] = { 1./6 * cb(1-f), 2./3 - 1./2 * sq(f)*(2-f), 2./3 - 1./2 * sq(1-f)*(2-(1-f)), 1./6 * cb(f) };
    auto X = [&](int index) { return clamp(0.f, measureX[clamp<int>(0, index, measureX.size-1)]/2 - sizeHint(0).x/2, image.size.y-sizeHint(0).x/2); };
    float newOffset = round( w[0]*X(index-1) + w[1]*X(index) + w[2]*X(index+1) + w[3]*X(index+2) );
    offset = newOffset;
    log(index);
    if(window) window->setTitle(str(index, t1, t, t2));
    break;
   }
  }
  if(previousOffset != offset) contentChanged = true;
  if(contentChanged) window->render();
  log((int64)expected.values.last().time/ticksPerSeconds, offset);
 }

 vec2 sizeHint(vec2) override { return vec2(1366, 768/*image.size.x*/); }

 shared<Graphics> graphics(vec2) override {
  window->backgroundColor = __builtin_nanf("");
  const Image& target = window->target;
  assert_(target.size.x < image.size.y && image.size.x <= target.size.y, target.size, image.size);
  for(int x: range(target.size.x)) for(int y: range(image.size.x))
   target[y*target.stride+x] = byte4(byte3(image[clamp(0, int(offset)+x, image.size.y-1)*image.size.x+y]), 0xFF);
  for(OCRNote note: highlight) {
      const int x0 = -offset+note.position.x/2;
      const int y0 = note.position.y/2;
      for(uint dy: range(templates[note.value].size.y/2)) for(uint dx: range(templates[note.value].size.x/2)) {
          uint a = 0xFF-templates[note.value](dx*2, dy*2);
          blend(target, x0+dx, y0+dy, note.color, a/255.f);
      }
  }
  return shared<Graphics>();
 }

 void previousTitle() {
  for(size_t index: range(1, files.size)) if(startsWith(files[index], title) && !startsWith(files[index-1], title)) {
   setTitle(section(files[index-1],'.', 0, 1));
   break;
  }
 }
 void nextTitle() {
  for(size_t index: range(files.size-1)) if(startsWith(files[index], title) && !startsWith(files[index+1], title)) {
   setTitle(section(files[index+1],'.', 0, 1));
   break;
  }
 }
#endif
} app;
