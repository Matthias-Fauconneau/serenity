#include "MusicXML.h"
#include "midi.h"
#include "audio.h"
#include "keyboard.h"
#include "png.h"
#include "interface.h"
#include "window.h"
#include "encoder.h"
#include "asound.h"
#include "render.h"
#include "text.h"
#include "sort.h"

Image8 toImage8(const Image& image) {
 return Image8(apply(image, [](byte4 bgr){
                return (uint8)((bgr.b+bgr.g+bgr.r)/3); // FIXME: coefficients ?
               }), image.size);
}
void toImage(const Image& image, const Image8& image8) {
 assert_(image.size == image8.size);
 if(image.ref::size == image8.ref::size) {
  for(uint i: range(image.ref::size)) { uint8 g=image8[i]; image[i] = byte4(g,g,g,0xFF); }
 } else {
  for(uint y: range(image.size.y)) for(uint x: range(image.size.x)) { uint8 g=image8(x,y); image(x,y) = byte4(g,g,g,0xFF); }
 }
}
Image toImage(const Image8& image8) { Image image(image8.size); toImage(image, image8); return image; }

Image8 downsample(Image8&& target, const Image8& source) {
 assert_(target.size == source.size/2, target.size, source.size);
 for(uint y: range(target.height)) for(uint x: range(target.width))
  target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / 4;
 return move(target);
}
inline Image8 downsample(const Image8& source) { return downsample(source.size/2, source); }

/// Converts MIDI time base to audio sample rate
MidiNotes scale(MidiNotes&& notes, uint targetTicksPerSeconds, int64 start) {
 assert_(notes);
 const int offset = start - (int64)notes.first().time*targetTicksPerSeconds/notes.ticksPerSeconds;
 for(MidiNote& note: notes) {
  note.time = offset + (int64)note.time*targetTicksPerSeconds/notes.ticksPerSeconds;
 }
 notes.ticksPerSeconds = targetTicksPerSeconds;
 return move(notes);
}

uint audioStart(string audioFileName) {
 assert_(audioFileName);
 for(FFmpeg file(audioFileName);;) {
  int32 buffer[1024  * file.channels];
  size_t size = file.read32(mref<int32>(buffer, 1024 * file.channels));
  for(size_t i: range(size *file.channels)) if(abs(buffer[i])>1<<23) {
   return file.audioTime+i;
  }
 }
}

struct Music : Widget {
 // MusicXML
 buffer<Sign> signs;
 // PNG
 Map rawImageFileMap;
 Image8 image;
 Image8 imageLo;
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
 //buffer<Sign> midiToSign;
 buffer<size_t> midiToSign;
 array<int64> measureT;
 // MP3
 unique<FFmpeg> audioFile = nullptr;

 // View
 Scroll<ImageView> scroll;
 Keyboard keyboard; // 1/6 ~ 120
 map<uint, Sign> active; // Maps active keys to notes (indices)
 uint midiIndex = 0, noteIndex = 0;
 buffer<Image8> templates;
 array<OCRNote> highlight;

 // Preview
 unique<Window> window = nullptr;
 Thread audioThread;
 AudioOutput audio = {{this, &Music::read32}, audioThread};

 const bool encode = arguments().contains("encode") || arguments().contains("export");

 size_t read32(mref<int2> output) {
  assert_(audio.channels == 2 && audioFile->channels == audio.channels && audioFile->audioFrameRate == audio.rate);
  audioFile->read32(mcast<int>(output));
  return output.size;
 }

 Music() {
  Time time{true};
  const String name = copyRef(section(Folder(".").name(), '/', -2, -1));
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
   writeFile(imageFile, cast<byte>(toImage8(image)));
  }
  rawImageFileMap = Map(imageFile);
  image = Image8(cast<uint8>(unsafeRef(rawImageFileMap)), size);
  templates = apply(3, [](int i){ return toImage8(decodePNG(readFile(str(i)+".png")));});

  Image target;
  if(!existsFile("OCRNotes") || File("0.png").modifiedTime() > File("OCRNotes").modifiedTime() || 0) {
   buffer<Image8> negatives = apply(templates.size, [](int i){
    return existsFile(str(i)+"-.png") ? toImage8(decodePNG(readFile(str(i)+"-.png"))) : Image8();
   });

   if(1) target = toImage(cropRef(image,0,int2(image.size.x, image.size.y)));
   uint sumX[image.size.y]; // Σ[x-Tx,x]
   const uint Tx = templates[0].size.x, Ty = templates[2].size.y;
   uint intensityThreshold[templates.size];
#if 0
   uint sum=0; for(uint x: range(Tx)) for(uint y: range(Ty)) sum += templates[1](x, y);
   for(uint t: range(templates.size)) intensityThreshold[t] = sum;
#else
   uint I0=0; for(uint x: range(Tx)) for(uint y: range(Ty)) I0 += templates[1](x, y);
   for(uint t: range(templates.size)) {
    uint sum = 0;
    for(uint x: range(templates[t].size.x)) for(uint y: range(templates[t].size.y)) sum += templates[t](x, y);
    int Dx = Tx-templates[t].size.x;
    int Dy = Ty-templates[t].size.y;
    if(Dy>0 && (uint)templates[t].size.x < Tx) sum += (int[]){0xC0,0xFF,0x40,0xC0}[t]*(templates[t].size.x*Dy); // Semi conservative cull
    if(Dx>0 && (uint)templates[t].size.y < Ty) sum += (int[]){0xC0,0xFF,0x40,0xC0}[t]*(templates[t].size.x*Dy); // Semi conservative cull
    if(Dx>0 && Dy>0) sum += (int[]){0xC0,0xFF,0x40,0xC0}[t]*(Dx*Dy); // Semi conservative cull
    //assert_(sum >= I0, sum, I0);
    //assert_(sum <= I0, sum, I0);
    //intensityThreshold[t] = I0; //sum;
    //intensityThreshold[t] = ::max(I0, sum); //sum;
    intensityThreshold[t] = ::min(I0, sum); //sum;
   }
#endif
   for(uint y: range(1,image.size.y)) { sumX[y]=0; for(uint x: range(1,Tx+1)) sumX[y] += image(x,y); }
   Image16 corrMap (image.size); corrMap.clear(0);
   Image8 localMax (image.size); localMax.clear(0);
   Time lastReport{true};
   for(uint x: range(Tx+1, target?target.size.x:image.size.x)) {
    if(lastReport.seconds()>1) { lastReport.reset(); log(strD(x, target.size.x)); };
    {
     uint sumY=0; // Σ[y-Ty,y]
     const int top = 192, bottom = 64;
     const int y0=top-Ty, y1=image.size.y-bottom;
     for(uint y: range(y0, y0+Ty)) {
      sumX[y] += image(x,y) - image(x-Tx,y);
      sumY += sumX[y];
     }
     for(uint y: range(y0+Ty, y1)) {
      sumX[y] += image(x,y) - image(x-Tx,y);
      sumY += sumX[y] - sumX[y-Ty];
      for(uint t: range(templates.size)) {
       if(sumY >= intensityThreshold[t]) continue;
       //if(t==0) target(x-templates[t].size.x+1,y-templates[t].size.x+1) = byte4(0xFF,0,0,0xFF);
       //const int correlationThreshold = int(Tx)*int(Ty)*sq(96);
       //const int correlationThreshold = (templates[t].size.x)*(templates[t].size.y)*sq((int[]){/*96*/104,96,104,104}[t]);
       const int correlationThreshold = int(Tx)*int(Ty)*sq((int[]){103/*97-103*/,88,104,104}[t]);
       int corr = 0;
       //const int x0 = x-templates[t].size.x+1, y0 = y-templates[t].size.y+1;
       const int x0 = x-Tx+1, y0 = y-Ty+1;
       const int half = 128;
       //for(uint dy: range(templates[t].size.y)) for(uint dx: range(templates[t].size.x)) {
       for(uint dy: range(Ty)) for(uint dx: range(Tx)) {
               corr += (int(image(x0+dx,y0+dy))-half) * (int(templates[t](dx, dy))-half);
       }
       if(corr < correlationThreshold) continue;
       int ncorr = 0;
       if(t==0) for(uint dy: range(negatives[t].size.y)) for(uint dx: range(negatives[t].size.x)) {
        ncorr += (int(image(x0+dx,y0+dy))-half) * (int(negatives[t](dx, dy))-half);
       }
       //if(corr*2 > ncorr*3) {
       if(corr*2 > ncorr*3) {
        corr /= 512;
        assert_(corr < 65536, corr);
        //corr /= 256;
        //assert_(corr < 256, corr);

        const int r = 6;
        for(int dy: range(-r, r)) for(uint dx: range(-r, r +1)) { // FIXME: -1,{-1,0,1}; 0,-1
         if(corrMap(x0+dx, y0+dy) >= corr) goto skip;
        }
        for(int dy: range(-r, r +1)) for(uint dx: range(-r, r +1)) {
         localMax(x0+dy, y0+dx) = 0; // Clears
         //target(x0+dy, y0+dx) = image(x0+dy,y0+dx);
        }
        //target(x0, y0) = byte4((byte3[]){byte3(0,0,0xFF), byte3(0,0xFF,0),byte3(0,0xFF,0xFF)}[t],0xFF);*/
        localMax(x0, y0) = 1+t;
skip:;
        if(corr > corrMap(x0, y0)) corrMap(x0, y0) = corr; // Helps early cull
       }
      }
     }
    }

    if(measureX && x<=measureX.last()+200*image.size.y/870) continue; // Minimum interval between measures
    uint sum = 0;
    for(uint y: range(image.size.y)) sum += image(x,y);
    if(sum < 255u*image.size.y*3/5) { // Measure Bar
     if(target) for(size_t y: range(image.size.y)) target(x,y) = 0;
     measureX.append(x);
    }
   }

   for(uint x0: range(1, localMax.size.x-Tx)) {
    for(uint y0: range(1, localMax.size.y-Ty)) {
     if(localMax(x0, y0) > 0) {
      int confidence = corrMap(x0, y0);
      int t = localMax(x0, y0)-1;
      OCRNotes.append({confidence, t, int2(x0, y0), black, invalid});
      // Visualization
      if(target) for(uint dy: range(templates[t].size.y)) for(uint dx: range(templates[t].size.x)) {
       uint a = 0xFF-templates[t](dx, dy);
       blend(target, x0+dx, y0+dy, (bgr3f[]){red, green, yellow, cyan}[t], a/255.f);
      }
     }
    }
   }

   writeFile("measureX", cast<byte>(measureX), currentWorkingDirectory(), true);
   writeFile("OCRNotes", cast<byte>(OCRNotes), currentWorkingDirectory(), true);
  } else if(0) target = toImage(cropRef(image,0,int2(image.size.x, image.size.y))); // DEBUG
  measureX = cast<uint>(readFile("measureX"));
  if(target) for(uint i: range(measureX.size)) render(target, Text(str(1+i),64).graphics(0), vec2(measureX[i], 64));
  OCRNotes = cast<OCRNote>(readFile("OCRNotes"));

  signs = MusicXML(readFile(name+".xml"_)).signs;

  map<uint, array<size_t>> notes; // skips tied (for MIDI)
  map<uint, array<size_t>> allNotes; // also tied (for OCR)
  for(size_t signIndex: range(signs.size)) {
   Sign& sign = signs[signIndex];
   if(sign.type == Sign::Note) {
    Note& note = sign.note;
    note.signIndex = signIndex;
    allNotes.sorted(sign.time).add( signIndex );
    if(note.tie == Note::NoTie || note.tie == Note::TieStart) {
     notes.sorted(sign.time).add( signIndex );
    }
   }
  }

  array<OCRNote> sorted; // Bins close X together
  array<OCRNote> bin; int lastX = 0;
  //assert_(templates.size==4);
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


  uint ocrIndex = 0; // X (Time), Top to Bottom
  //array<char> strNotes;
  for(ref<size_t> chord: allNotes.values) {
   for(size_t signIndex: chord.reverse()) { // Top to Bottom
    Sign& note = signs[signIndex];
    if(note.note.grace) continue;
    // Transfers to notes
    bool tied = false;
    note.note.glyphIndex[0] = ocrIndex; // FIXME: dot, accidentals
    for(mref<size_t> chord: notes.values) for(size_t oIndex: chord) {
     Sign& o = signs[oIndex];
     if((size_t)o.note.tieStartNoteIndex == signIndex) {
      for(int i=1;;i++) {
       assert_(i < 4);
       assert_(o.note.glyphIndex[i]!=ocrIndex);
       if(o.note.glyphIndex[i]==invalid) { o.note.glyphIndex[i] = ocrIndex; tied=true; break; } // Also highlights tied notes
      }
     }
    }
    if(0 && target && ocrIndex < OCRNotes.size) render(target, Text(strKey(2,note.note.key()),64,tied?green:red).graphics(0), vec2(OCRNotes[ocrIndex].position)); // DEBUG
    ocrIndex++;
    //strNotes.append(strKey(2,note.note.key())+" ");
   }
  }
  //log(strNotes);

  if(ocrIndex != OCRNotes.size) log(ocrIndex, OCRNotes.size);
  if(0 && target) { writeFile("debug.png", encodePNG(target), currentWorkingDirectory(), true); return; }
  //assert_(glyphIndex <= OCRNotes.size, glyphIndex, OCRNotes.size);
  //assert_(glyphIndex == OCRNotes.size, glyphIndex, OCRNotes.size);

  String audioFileName = existsFile(name+".m4a") ? name+".m4a" : name+".mp3";
  audioFile = unique<FFmpeg>(audioFileName);
  assert_(audioFile->audioFrameRate);

  this->notes = ::scale(MidiFile(readFile(name+".mid"_)).notes, audioFile->audioFrameRate, audioStart(audioFileName));
  buffer<MidiNote> midiNotes = filter(this->notes, [](MidiNote o){return o.velocity==0;});

  // Associates MIDI notes with score notes
  //midiToSign = buffer<Sign>(midiNotes.size, 0);
  midiToSign = buffer<size_t>(midiNotes.size, 0);

  array<array<uint>> S;
  array<array<uint>> Si; // Sign indices (sorted)
  uint64 minT=-1; {
   uint lastTime = 0;
   array<uint> bin;
   array<uint> binS; // Sign indices
   array<uint> binI;
   for(ref<size_t> chord: notes.values) {
    for(size_t signIndex: chord) {
     //log(note.time-lastTime);
     Sign note = signs[signIndex];
     if(note.time-lastTime > 0/*84*/) {
      minT = ::min(minT, note.time-lastTime);
      array<uint> binSorted = copyRef(bin);
      sort(binSorted); // FIXME: already sorted ?
      for(size_t key: binSorted) binI.append(bin.indexOf(key));
      S.append(move(binSorted));
      //Si.append(move(binI));
      Si.append(apply(binI, [&binS](uint i){ return binS[i]; }));
      bin.clear();
      binI.clear();
      binS.clear();
     }
     lastTime = note.time;
     if(!bin.contains(note.note.key())) {
      bin.append(note.note.key());
      binS.append(note.note.signIndex);
     }
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
   while(index < midiNotes.size && (int64(midiNotes[index].time) <= time+2119 /*2048-2119*/
                                   //|| (int64(midiNotes[index].time) <= time+5436 && bin.size < S[::min(M.size, S.size-1)].size)
                                    ) ) {
    if(0) log(int64(midiNotes[index].time) - time, strKey(2, midiNotes[index].key));
    bin.append(midiNotes[index].key); // add
    binT.append(midiNotes[index].time);
    index++;
   }
   if(0) if(index < midiNotes.size) log(int64(midiNotes[index].time) - time, strKey(2, midiNotes[index].key));
   array<uint> binSorted = copyRef(bin);
   sort(binSorted);
   for(size_t key: bin) binI.append(binSorted.indexOf(key));
   if(0 && S[min(M.size, S.size-1)] != binSorted) {
    log("≠");
    if(index < midiNotes.size) log(int64(midiNotes[index].time) - time, strKey(2, midiNotes[index].key));
    log(apply(S[min(M.size, S.size-1)], [](uint key){return strKey(2, key);}),":", apply(binSorted, [](uint key){return strKey(2, key);}),
    apply(binI, [&](uint i){return int(binT[binI[i]]-binT[binI[0]]);}));
    log("≠");
    //error(minT);
   } else if(0) log(apply(S[min(M.size, S.size-1)], [](uint key){return strKey(2, key);}),"=", apply(binSorted, [](uint key){return strKey(2, key);}));

   //assert_(S[min(M.size, S.size-1)] == binS, "≠");
   if(0 && M.size < S.size && S[min(M.size, S.size-1)] != binSorted && S[min(M.size, S.size-1)].size == 1 && binSorted.size == 1) { // FIXME: let sync handle missing MIDI notes
    log("M");
    Mi.append();
    M.append(); // Missing in MIDI
    index--; // Rewind to match next time
    continue;
   }
   Mi.append(move(binI));
   M.append(move(binSorted));
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
  size_t lastSignIndex = 0;
  measureT.append(0);
  //size_t errors = 0;
  if(0) log("|", measureT.last(), measureX[measureT.size-1]);
  log("|", measureT.size);
  while(i<m && j<n) {
   /**/ if(i+1<m && D(i,j) == D(i+1,j)) { // in Score not in MID
    //error("S", apply(S[i], [](uint key){return strKey(2, key);}));
    if(1) log("S", apply(S[i], [](uint key){return strKey(2, key);}));
    i++;
   }
   else if(j+1<n && D(i,j) == D(i,j+1)) {
    for(size_t unused k: range(M[j].size)) midiToSign.append(invalid); //midiToSign.append(Sign{});
    if(1) log(measureT.size, "M", apply(M[j], [](uint key){return strKey(2, key);})); // Trills, tremolos
    j++;
   } else {
    array<uint> midiKeys;
    bool measureChanged = false;
    for(size_t k: range(M[j].size)) {
     size_t midiIndex = midiToSign.size;

     //Sign sign{};
     size_t signIndex = invalid;
     if(Mi[j][k]<Si[i].size) {
      //sign = signs[Si[i][Mi[j][k]]];
      signIndex = Si[i][Mi[j][k]];
      assert_(signIndex != invalid);
     } else log("Missing note", strKey(2, midiNotes[midiIndex].key), Mi[j][k], Si[i].size, Si[i][Mi[j][k]], notes.values[i].size);
     /*log(strKey(2,sign.note.key()), strKey(2, this->notes[midiToSign.size].key));
     if(sign.note.key()%12 != this->notes[midiToSign.size].key%12) {
      log("!", errors++);
     }*/
     midiKeys.append(midiNotes[midiIndex].key);
     //midiToSign.append( sign ); // Maps original MIDI index to sorted to original note index
     midiToSign.append( signIndex ); // Maps original MIDI index to sorted to original note index
     if(/*sign.note.*/signIndex != invalid) {
      size_t nextSignIndex = /*sign.note.*/signIndex;
      for(;lastSignIndex < nextSignIndex;lastSignIndex++) {
       Sign sign = signs[lastSignIndex];
       if(sign.type == Sign::Measure) {
        measureT.append(midiNotes[midiIndex].time);
        measureChanged = true;
       }
      }
     }
    }
    if(0) log(apply(S[i], [](uint key){return strKey(2, key);}), "=", apply(M[j], [](uint key){return strKey(2, key);}),
     //apply(Si[i], [this](uint i){ assert_(i<signs.size,i); return signs[i]; }));
     apply(Si[i], [this](uint i){ assert_(i<signs.size,i); return signs[i].note.glyphIndex[0]; }));
    if(1 && measureChanged) log("|", measureT.size);
    if(1) log(apply(S[i], [](uint key){return strKey(2, key);}), "=", apply(M[j], [](uint key){return strKey(2, key);}));
    //if(0) log(apply(S[i], [](uint key){return strKey(2, key);}), "=", apply(M[j], [](uint key){return strKey(2, key);}));
    //if(0) log(apply(S[i], [](uint key){return strKey(2, key);}), "=", apply(M[j], [](uint key){return strKey(2, key);}), "=", apply(midiKeys, [](uint key){return strKey(2, key);}));
    i++; j++;
   }
  }
  assert_(j == n);
  //for(;j<n;j++) for(size_t unused k: range(M[j].size)) midiToSign.append(Sign{});
  /*// Last measure bar
  for(;signIndex < signs.size;signIndex++) {
   Sign sign = signs[signIndex];
   if(sign.type == Sign::Measure) {
    measureT.append(midiNotes[midiIndex].time);
   }
  }*/
  measureT.append(this->notes.last().time);
  if(midiToSign.size != midiNotes.size) log(midiNotes.size, midiToSign.size);
  //assert_(midiToSign.size == midiNotes.size, midiNotes.size, midiToSign.size);
  //assert_(measureT.size == measureX.size, measureT.size, measureX.size);
  // Removes skipped measure explicitly (so that scroll smoothes over, instead of jumping)
  if(1) for(uint i=0; i<measureT.size-1;) {
   if(measureT[i] == measureT[i+1]) {
    log("-", i+1, measureT[i], measureX[i]);
    measureT.removeAt(i);
    measureX.removeAt(i);
    //uint x = measureX.take(i+1); measureX[i] = (measureX[i] + x) / 2; // New position in middle of skipped measure
   } else i++;
  }
  //scroll.image = unsafeRef(image);
  scroll.horizontal=true, scroll.vertical=false, scroll.scrollbar = true;
  imageLo = downsample(image);

  if(0 && target) {
   for(uint midiIndex=0;midiIndex < this->notes.size /*&& (int64)notes[midiIndex].time*timeDen <= timeNum*(int64)notes.ticksPerSeconds*/; midiIndex++) {
    const MidiNote note = this->notes[midiIndex];
    if(note.velocity) {
     //assert_(noteIndex < sheet.midiToSign.size, noteIndex, sheet.midiToSign.size);
     //const Sign sign = midiToSign[noteIndex];
     if(midiToSign[noteIndex] != invalid) {
      const Sign sign = signs[midiToSign[noteIndex]];
      const size_t glyphIndex = sign.note.glyphIndex[0];
      //log(note.key, sign.note.key());
      //assert_(note.key == sign.note.key());
      if(target && glyphIndex < OCRNotes.size)
       render(target, Text(strKey(2,note.key),64).graphics(0), vec2(OCRNotes[glyphIndex].position));
     }
     noteIndex++;
    }
   }
   writeFile("debug.png", encodePNG(target), currentWorkingDirectory(), true);
   return;
  }

  if(encode) { // Encode
   assert_(!existsFile(name+".mp4"));
   Encoder encoder {name+".mp4"_};
   encoder.setH264(int2(1920, 1080), 60);
   if(audioFile && (audioFile->codec==FFmpeg::AAC || audioFile->codec==FFmpeg::MP3 || audioFile->codec==FFmpeg::Opus)) encoder.setAudio(audioFile);
   else error("Unknown codec");
   encoder.open();

   uint videoTime = 0;

   Time resampleTime, renderTime, videoEncodeTime, totalTime;
   totalTime.start();
   for(int lastReport=0;;) {
    auto writeAudio = [&] {
     if(audioFile && (audioFile->codec==FFmpeg::AAC || audioFile->codec==FFmpeg::MP3)) {
      return encoder.copyAudioPacket(audioFile);
     } else if(audioFile) {
      assert_(encoder.audioFrameSize==1024);
      buffer<int16> source(encoder.audioFrameSize*audioFile->channels);
      const size_t readSize = audioFile->read16(source);
      assert_(readSize == encoder.audioFrameSize);
      buffer<int16> target(readSize*encoder.channels);
      if(audioFile->channels == 1 && encoder.channels == 2) {
       for(size_t i: range(readSize)) target[i*2+0] = target[i*2+1] = source[i];
      } else assert_(audioFile->channels == encoder.channels);
      if(target.size) encoder.writeAudioFrame(target);
     }
     return true;
    };
    if(encoder.videoFrameRateNum) { // Interleaved AV
     //assert_(encoder.audioStream->time_base.num == 1 && encoder.audioStream->time_base.den == (int)encoder.audioFrameRate);
     // If both streams are at same PTS, starts with audio
     bool done = false;
     while((int64)encoder.audioTime*encoder.videoFrameRateNum <= (int64)encoder.videoTime*encoder.audioFrameRate*encoder.videoFrameRateDen) {
      if(!writeAudio()) { done = true; break /*2*/; }
     }
     if(done) { log("Audio track end"); break; }
     Image target (encoder.size);
     target.clear(0xFF);
     while((int64)encoder.videoTime*encoder.audioFrameRate*encoder.videoFrameRateDen <= (int64)encoder.audioTime*encoder.videoFrameRateNum) {
      follow(videoTime*encoder.videoFrameRateDen, encoder.videoFrameRateNum, vec2(encoder.size));
      renderTime.start();
      assert_(encoder.size.y >= image.size.y/2/*+keyboard.sizeHint(0).y*/, encoder.size.y, image.size.y);
      const int width = ::min((image.size.x-(int)(-scroll.offset.x))/2, encoder.size.x);
      const int height = ::min(target.size.y-image.size.y/2, 240);
      const int y0 = (target.size.y-height-image.size.y/2)/2;
      //const int y1 = y0+image.size.y/2, y2=target.size.y;
      resampleTime.start();
      const Image8 source = cropRef(imageLo, int2(-scroll.offset.x/2, 0), int2(width, image.size.y/2));
      const Image subTarget = cropRef(target, int2(0, y0),  int2(width, image.size.y/2));
      /*toImage(subTarget, downsample(source));*/
      for(size_t y: range(source.size.y)) {
          const uint8* const sourceLine =    source.begin()+y*   source.stride;
                byte4* const targetLine = subTarget.begin()+y*subTarget.stride;
          for(size_t x: range(source.size.x)) targetLine[x] = sourceLine[x];
      }
      resampleTime.stop();
      //fill(target, int2(width, 0), int2(target.size.x-width, image.size.y/2), white, 1);
      for(OCRNote note: highlight) {
       const int x = scroll.offset.x+note.position.x;
       const int y = scroll.offset.y+note.position.y;
       Image8 t = downsample(templates[note.value]);
       for(uint dy: range(t.size.y)) for(uint dx: range(t.size.x)) {
        uint a = 0xFF-t(dx, dy);
        blend(target, x/2+dx, y0+y/2+dy, note.color, a/255.f);
       }
      }
      //fill(target, int2(0, y1), int2(target.size.x, (y2-height)-y1), white);
      keyboard.render(cropRef(target, int2(0, target.size.y-height), int2(target.size.x, height)));
      renderTime.stop();
      videoEncodeTime.start();
      encoder.writeVideoFrame(target);
      videoEncodeTime.stop();
      videoTime++;
     }
    } else { // Audio only
     if(!writeAudio()) { log("Audio track end"); break; }
    }
    uint64 timeTicks;
    if(encoder.videoFrameRateNum) timeTicks = (uint64)videoTime*encoder.videoFrameRateDen*this->notes.ticksPerSeconds/encoder.videoFrameRateNum;
    else if(encoder.audioFrameRate) timeTicks = (uint64)encoder.audioTime*this->notes.ticksPerSeconds/encoder.audioFrameRate;
    else error("");
    //buffer<uint> onsets = apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.time;});
    //uint64 durationTicks = onsets.last() + 4*notes.ticksPerSeconds;
    uint64 durationTicks = this->notes.last().time;
    int percent = round(100.*timeTicks/durationTicks);
    if(percent!=lastReport) {
     log(str(percent, 2u)+"%", "Resample", strD(resampleTime, totalTime), "Render", strD(renderTime, totalTime), "Encode", strD(videoEncodeTime, totalTime)
         /*,int(round((float)totalTime*((float)durationTicks/timeTicks-1))), "/", int(round((float)totalTime/timeTicks*durationTicks)), "s"*/);
     lastReport=percent;
    }
    if(timeTicks >= durationTicks+2*this->notes.ticksPerSeconds/*2sec fadeout*/) break;
    //if(video && video.videoTime >= video.duration) break;
    //if(timeTicks > 4*this->notes.ticksPerSeconds) break; // DEBUG
   }
   log("Done");
  } else { // Preview
   window = ::window(this);
   audio.start(audioFile->audioFrameRate, 1024, 32, 2);
   audioThread.spawn();
  }
 }

 bool follow(int64 timeNum, int64 timeDen, vec2 size) {
  //constexpr int staffCount = 2;
  bool contentChanged = false;
  //log(midiIndex, notes.size, (float)notes[midiIndex].time/notes.ticksPerSeconds, (float)timeNum/timeDen);
  for(;midiIndex < notes.size && (int64)notes[midiIndex].time*timeDen <= timeNum*(int64)notes.ticksPerSeconds; midiIndex++) {
   MidiNote note = notes[midiIndex];
   if(note.velocity) {
    //assert_(noteIndex < sheet.midiToSign.size, noteIndex, sheet.midiToSign.size);
    //Sign sign = midiToSign[noteIndex];
    //Sign sign = signs[midiToSign[noteIndex]];
    if(midiToSign[noteIndex] != invalid) {
     Sign sign = signs[midiToSign[noteIndex]];
     assert_(sign.type == Sign::Note);
     // Removes trills highlight on known (marked) note press of same key
     if(active.contains(note.key) && active.at(note.key).note.trill) {
      Sign sign = active.take(note.key);
      (sign.staff?keyboard.left:keyboard.right).remove( sign.note.key() );
      for(int i: range(4)) if(sign.note.glyphIndex[i] != invalid) highlight.remove(sign.note.glyphIndex[i]);
     }
     active.insertMulti(note.key, sign);
     (sign.staff?keyboard.left:keyboard.right).append( sign.note.key() );
     for(int i: range(4)) {
      if(sign.note.glyphIndex[i] != invalid) {
       OCRNote note = OCRNotes[sign.note.glyphIndex[i]];
       note.glyphIndex = sign.note.glyphIndex[i]; // For removal
       note.color = (sign.staff?red:green);
       highlight.append(note);
      }
     }
    } else keyboard.unknown.append( note.key );
    contentChanged = true;
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
   else if(!note.velocity) {
    if(active.contains(note.key)) {
     while(active.contains(note.key) && !active.at(note.key).note.trill) {
      if(active.at(note.key).note.trill) continue; // Will be removed on known (marked) note press of same key
      Sign sign = active.take(note.key);
      (sign.staff?keyboard.left:keyboard.right).remove( sign.note.key() );
      for(int i: range(4)) if(sign.note.glyphIndex[i] != invalid) highlight.remove(sign.note.glyphIndex[i]);
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
     }
     contentChanged = true;
    }
    while(keyboard.unknown.contains(note.key)) keyboard.unknown.remove(note.key);
   }
  }

  int64 t = (int64)timeNum*(int64)notes.ticksPerSeconds;
  float previousOffset = scroll.offset.x;
   // Cardinal cubic B-Spline
  for(int index: range(measureT.size-1)) {
   int64 t1 = (int64)measureT[index]*(int64)timeDen;
   int64 t2 = (int64)measureT[index+1]*(int64)timeDen;
   if(t1 <= t && t < t2) {
    double f = double(t-t1)/double(t2-t1);
    assert_(f >= 0 && f <= 1);
    double w[4] = { 1./6 * cb(1-f), 2./3 - 1./2 * sq(f)*(2-f), 2./3 - 1./2 * sq(1-f)*(2-(1-f)), 1./6 * cb(f) };
    auto X = [&](int index) { return clamp(0.f, measureX[clamp<int>(0, index, measureX.size-1)] - size.x/2, image.size.x-size.x); };
    float newOffset = round( w[0]*X(index-1) + w[1]*X(index) + w[2]*X(index+1) + w[3]*X(index+2) );
    scroll.offset.x = -newOffset;
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

 vec2 sizeHint(vec2) override { return vec2(1920/*/2*/, /*1440/2+240*/1080); }
 Time resampleTime, renderTime, totalTime{true};
 shared<Graphics> graphics(vec2 unused size) override {
     renderTime.start();
     follow(audioFile->audioTime, audioFile->audioFrameRate, vec2(window->size));
     window->render();
     //return scroll.ScrollArea::graphics(size);
     window->backgroundColor = __builtin_nanf("");
     assert_(-scroll.offset.x >= 0, scroll.offset.x);
     const Image& target = window->target;
     int width = ::min(image.size.x-(int)(-scroll.offset.x), (int)(uint64)target.size.x*2);
     const int y1 = image.size.y/2, y2=target.size.y;
     const int height = ::min(y2-y1, 240);
     const int y0 = (target.size.y-height-image.size.y/2)/2;
     assert_(y0 >= 0);
     resampleTime.start(); // FIXME: single pass downsample and toImage
     const Image targetArea = cropRef(target, int2(0, y0), int2(::min(target.size.x, (int)(uint64)width/2), image.size.y/2));
     //const Image8 source = cropRef(image, int2(-scroll.offset.x, 0), int2(width, image.size.y));
     //bilinear(targetArea, toImage(source));
     /*bilinear(cropRef(target, int2(0, (target.size.y-height-image.size.y)/2), int2(::min(target.size.x, (int)(uint64)width/2), image.size.y/2)),
              toImage(cropRef(image, int2(-scroll.offset.x, 0), int2(width, image.size.y)))); // FIXME: bilinear8*/
     const Image8 source = cropRef(imageLo, int2(-scroll.offset.x/2, 0), int2(width/2, image.size.y/2));
     for(size_t y: range(source.size.y)) for(size_t x: range(source.size.x)) targetArea(x, y) = byte4(byte3(source(x, y)),0xFF);
     resampleTime.stop();
     fill(target, int2(width, 0), int2(target.size.x-width, target.size.y), white, 1);
     for(OCRNote note: highlight) {
         const int x0 = scroll.offset.x+note.position.x;
         const int y0 = scroll.offset.y+note.position.y;
         for(uint dy: range(templates[note.value].size.y/2)) for(uint dx: range(templates[note.value].size.x/2)) {
             uint a = 0xFF-templates[note.value](dx*2, dy*2);
             blend(target, x0/2+dx, y0/2+dy, note.color, a/255.f);
         }
     }
     if(1) for(uint i: range(measureX.size)) render(target, Text(str(1+i),32).graphics(0), vec2((scroll.offset.x+measureX[i])/2, 32));
     keyboard.render(cropRef(target, int2(0, /*image.size.y/2*/target.size.y-height), int2(target.size.x, /*target.size.y-image.size.y/2*/height)));
     renderTime.stop();
     //log(strD(resampleTime, totalTime), strD(renderTime, totalTime));
     return shared<Graphics>();
 }
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) override {
return scroll.ScrollArea::mouseEvent(cursor, size, event, button, focus);
 }
 bool keyPress(Key key, Modifiers modifiers) override { return scroll.ScrollArea::keyPress(key, modifiers); }
} test;
