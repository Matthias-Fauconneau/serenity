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

Image8 toImage8(const Image& image) {
 return Image8(apply(image, [](byte4 bgr){
                //assert_(bgr.b==bgr.g && bgr.g==bgr.r, bgr.b, bgr.g, bgr.r);
                return (uint8)((bgr.b+bgr.g+bgr.r)/3); // FIXME: coefficients ?
               }), image.size);
}
void toImage(const Image& image, const Image8& image8) {
 assert_(image.size == image8.size);
 if(image.ref::size == image8.ref::size) {
  //assert_(image.ref::size == image8.ref::size, image.ref::size, image8.ref::size);
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
 buffer<Sign> midiToSign;
 array<int64> measureT;
 // MP3
 unique<FFmpeg> audioFile = nullptr;

 // View
 Scroll<ImageView> scroll;
 Keyboard keyboard; // 1/6 ~ 120
 map<uint, Sign> active; // Maps active keys to notes (indices)
 uint midiIndex = 0, noteIndex = 0;
 float playbackLineX = 0;
 buffer<Image8> templates;

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
   writeFile(imageFile, cast<byte>(toImage8(image)), Folder(name));
  }
  rawImageFileMap = Map(imageFile, Folder(name));
  //image = Image(cast<byte4>(unsafeRef(rawImageFileMap)), size);
  image = Image8(cast<uint8>(unsafeRef(rawImageFileMap)), size);

  templates = apply(3, [name](int i){ return toImage8(decodePNG(readFile(str(i)+".png", Folder(name))));});

  Image target;
  if(!existsFile("OCRNotes",name) || 0) {
   buffer<Image8> negatives = apply(3, [name](int i){ return toImage8(decodePNG(readFile(str(i)+"-.png", Folder(name))));});

   if(1) target = toImage(cropRef(image,0,int2(image.size.x, image.size.y)));
   uint sumX[image.size.y]; // Σ[x-Tx,x]
   const uint Tx = templates[0].size.x, Ty = templates[2].size.y;
   uint intensityThreshold = 0; for(uint x: range(Tx)) for(uint y: range(Ty)) intensityThreshold += templates[1](x, y);
   for(uint y: range(1,image.size.y)) { sumX[y]=0; for(uint x: range(1,Tx+1)) sumX[y] += image(x,y); }
   Image16 corrMap (image.size); corrMap.clear(0);
   Image8 localMax (image.size); localMax.clear(0);
   Time lastReport{true};
   for(uint x: range(Tx+1, target?target.size.x:image.size.x)) {
    if(lastReport.seconds()>1) { lastReport.reset(); log(strD(x, target.size.x)); };
    {
     uint sumY=0; // Σ[y-Ty,y]
     const int y0=256-Ty, y1=image.size.y-128;
     for(uint y: range(y0, y0+Ty)) {
      sumX[y] += image(x,y) - image(x-Tx,y);
      sumY += sumX[y];
     }
     for(uint y: range(y0+Ty, y1)) {
      sumX[y] += image(x,y) - image(x-Tx,y);
      sumY += sumX[y] - sumX[y-Ty];
      if(sumY >= intensityThreshold) continue;
      //target(x-Tx+1,y-Ty+1) = byte4(0xFF,0,0,0xFF);
      for(uint t: range(3)) {
       //const int correlationThreshold = int(Tx)*int(Ty)*sq(96);
       const int correlationThreshold = int(Tx)*int(Ty)*sq((int[]){96,96,104}[t]);
       int corr = 0;
       const int x0 = x-Tx+1, y0 = y-Ty+1;
       for(uint dy: range(Ty)) for(uint dx: range(Tx)) {
        corr += (int(image(x0+dx,y0+dy))-128) * (int(templates[t](dx, dy))-128);
       }
       if(corr < correlationThreshold) continue;
       int ncorr = 0;
       if(t==0 || t==2) for(uint dy: range(Ty)) for(uint dx: range(Tx)) {
        ncorr += (int(image(x0+dx,y0+dy))-128) * (int(negatives[t](dx, dy))-128);
       }
       if(corr*2 > ncorr*3) {
        corr /= 512;
        assert_(corr < 65536, corr);

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
      if(target) for(uint dy: range(Ty)) for(uint dx: range(templates[t].size.x)) {
       uint a = 0xFF-templates[t](dx, dy);
       blend(target, x0+dx, y0+dy, (bgr3f[]){red, green, yellow}[t], a/255.f);
      }
     }
    }
   }

   writeFile("measureX", cast<byte>(measureX), name, true);
   writeFile("OCRNotes", cast<byte>(OCRNotes), name, true);
  } else if(0) target = toImage(cropRef(image,0,int2(image.size.x, image.size.y)));
  measureX = cast<uint>(readFile("measureX", name));
  OCRNotes = cast<OCRNote>(readFile("OCRNotes", name));

  signs = MusicXML(readFile(name+".xml"_, Folder(name))).signs;

  map<uint, array<Sign>> notes; // skips tied (for MIDI)
  map<uint, array<Sign>> allNotes; // also tied (for OCR)
  for(size_t signIndex: range(signs.size)) {
   Sign sign = signs[signIndex];
   if(sign.type == Sign::Note) {
    Note& note = sign.note;
    note.signIndex = signIndex;
    allNotes.sorted(sign.time).append( sign );
    if(note.tie == Note::NoTie || note.tie == Note::TieStart) {
     notes.sorted(sign.time).append( sign );
    }
   }
  }

  array<OCRNote> sorted; // Bins close X together
  array<OCRNote> bin; int lastX = 0;
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
    if(target && glyphIndex < OCRNotes.size) render(target, Text(strKey(-4,note.note.key()),64).graphics(0), vec2(OCRNotes[glyphIndex].position)); // DEBUG
    glyphIndex++;
   }
  }

  if(glyphIndex != OCRNotes.size) log(glyphIndex, OCRNotes.size);
  if(target) { writeFile("debug.png", encodePNG(target), home(), true); return; }

  assert_(glyphIndex <= OCRNotes.size, glyphIndex, OCRNotes.size);
  //assert_(glyphIndex == OCRNotes.size, glyphIndex, OCRNotes.size);

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
   for(Sign note: chord) bin.add(note.note.key());
   array<uint> binS = copyRef(bin);
   sort(binS); // FIXME: already sorted ?
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
   while(index < midiNotes.size && (int64(midiNotes[index].time) < time+2000 ||
                                   (int64(midiNotes[index].time) <= time+/*2137*//*3091*//*6455*//*9637*//*10091*//*10818*//*12273*//*12999*//*13681*/16985 && S[min(M.size, S.size-1)].contains(midiNotes[index].key)
                                     && bin.size < S[min(M.size, S.size-1)].size)) ) { //FIXME: merge bins at sync time
    if(0) log(int64(midiNotes[index].time) - time, strKey(-4, midiNotes[index].key));
    bin.add(midiNotes[index].key);
    index++;
   }
   if(0) if(index < midiNotes.size) log(int64(midiNotes[index].time) - time, strKey(-4, midiNotes[index].key));
   array<uint> binS = copyRef(bin);
   sort(binS);
   if(0) log(apply(S[min(M.size, S.size-1)], [](uint key){return strKey(-4, key);}),":", apply(binS, [](uint key){return strKey(-4, key);}));
   if(S[min(M.size, S.size-1)] != binS) {
       log("≠");
       if(index < midiNotes.size) log(int64(midiNotes[index].time) - time, strKey(-4, midiNotes[index].key));
       log(apply(S[min(M.size, S.size-1)], [](uint key){return strKey(-4, key);}),":", apply(binS, [](uint key){return strKey(-4, key);}));
       log("≠");
   }
   //assert_(S[min(M.size, S.size-1)] == binS, "≠");
   for(size_t key: bin) binI.append(binS.indexOf(key));
   Mi.append(move(binI));
   if(M.size < S.size && S[min(M.size, S.size-1)] != binS && S[min(M.size, S.size-1)].size == 1 && binS.size == 1) { // FIXME: let sync handle missing MIDI notes
       M.append(); // Missing in MIDI
       index--; // Rewind to match next time
       continue;
   }
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
  if(0) log("|", measureT.last(), measureX[measureT.size-1]);
  while(i<m && j<n) {
   /**/ if(i+1<m && D(i,j) == D(i+1,j)) {
    error("S", apply(S[i], [](uint key){return strKey(-4, key);}));
    if(1) log("S", apply(S[i], [](uint key){return strKey(-4, key);}));
    i++;
   }
   else if(j+1<n && D(i,j) == D(i,j+1)) {
    for(size_t unused k: range(M[j].size)) midiToSign.append(Sign{});
    if(0) log("M", apply(M[j], [](uint key){return strKey(-4, key);})); // Trills, tremolos
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
       if(sign.type == Sign::Measure) {
        measureT.append(midiNotes[midiIndex].time);
        if(0) log("|", measureT.last(), measureX[measureT.size-1]);
       }
      }
     }
    }
    if(0) log(apply(S[i], [](uint key){return strKey(-4, key);}), "=", apply(M[j], [](uint key){return strKey(-4, key);}));
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
  assert_(measureT.size == measureX.size, measureT.size, measureX.size);
  // Removes skipped measure explicitly (so that scroll smoothes over, instead of jumping)
  if(1) for(uint i=0; i<measureT.size-1;) {
   if(measureT[i] == measureT[i+1]) {
    error("-", i, measureT[i], measureX[i]);
    measureT.removeAt(i);
    measureX.removeAt(i);
    //uint x = measureX.take(i+1); measureX[i] = (measureX[i] + x) / 2; // New position in middle of skipped measure
   } else i++;
  }
  //scroll.image = unsafeRef(image);
  scroll.horizontal=true, scroll.vertical=false, scroll.scrollbar = true;
  imageLo = downsample(image);

  if(encode) { // Encode
   Encoder encoder {name+".tutorial.mp4"_};
   encoder.setH264(int2(1920, 1080), 60);
   if(audioFile && (audioFile->codec==FFmpeg::AAC || audioFile->codec==FFmpeg::MP3)) encoder.setAudio(audioFile);
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
     while((int64)encoder.videoTime*encoder.audioFrameRate*encoder.videoFrameRateDen <= (int64)encoder.audioTime*encoder.videoFrameRateNum) {
      follow(videoTime*encoder.videoFrameRateDen, encoder.videoFrameRateNum, vec2(encoder.size));
      renderTime.start();
      assert_(encoder.size.y >= image.size.y/2/*+keyboard.sizeHint(0).y*/, encoder.size.y, image.size.y);
      const int width = ::min((image.size.x-(int)(-scroll.offset.x))/2, encoder.size.x);
      const int height = ::min(target.size.y-image.size.y/2, 240);
      const int y0 = (target.size.y-height-image.size.y/2)/2;
      const int y1 = y0+image.size.y/2, y2=target.size.y;
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
      fill(target, int2(width, 0), int2(target.size.x-width, image.size.y/2), white, 1);
      for(OCRNote note: highlight) {
       const int x = scroll.offset.x+note.position.x;
       const int y = scroll.offset.y+note.position.y;
       Image8 t = downsample(templates[note.value]);
       for(uint dy: range(t.size.y)) for(uint dx: range(t.size.x)) {
        uint a = 0xFF-t(dx, dy);
        blend(target, x/2+dx, y0+y/2+dy, note.color, a/255.f);
       }
      }
      fill(target, int2(0, y1), int2(target.size.x, (y2-height)-y1), white);
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
    if(timeTicks >= durationTicks+this->notes.ticksPerSeconds/*1sec fadeout*/) break;
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

 array<OCRNote> highlight;

 bool follow(int64 timeNum, int64 timeDen, vec2 size) {
  //constexpr int staffCount = 2;
  bool contentChanged = false;
  //log(midiIndex, notes.size, (float)notes[midiIndex].time/notes.ticksPerSeconds, (float)timeNum/timeDen);
  for(;midiIndex < notes.size && (int64)notes[midiIndex].time*timeDen <= timeNum*(int64)notes.ticksPerSeconds; midiIndex++) {
   MidiNote note = notes[midiIndex];
   if(note.velocity) {
    //assert_(noteIndex < sheet.midiToSign.size, noteIndex, sheet.midiToSign.size);
    Sign sign = midiToSign[noteIndex];
    if(sign.type == Sign::Note) {
     // Removes trills highlight on known (marked) note press of same key
     if(active.contains(note.key) && active.at(note.key).note.trill) {
      Sign sign = active.take(note.key);
      (sign.staff?keyboard.left:keyboard.right).remove( sign.note.key() );
      for(int i: range(2)) if(sign.note.glyphIndex[i] != invalid) highlight.remove(sign.note.glyphIndex[i]);
     }
     active.insertMulti(note.key, sign);
     (sign.staff?keyboard.left:keyboard.right).append( sign.note.key() );
     for(int i: range(2)) {
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
      for(int i: range(2)) if(sign.note.glyphIndex[i] != invalid) highlight.remove(sign.note.glyphIndex[i]);
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
  float previousPlaybackLineX = playbackLineX;
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
    /*if(newOffset >= -scroll.offset.x)*/ scroll.offset.x = -newOffset;
    playbackLineX = (1-f) * measureX[index] + f * measureX[index+1];
    break;
   }
  }
  if(previousOffset != scroll.offset.x || previousPlaybackLineX != playbackLineX) contentChanged = true;
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
     resampleTime.start(); // FIXME: single pass downsample and toImage
     const Image targetArea = cropRef(target, int2(0, (target.size.y-height-image.size.y)/2), int2(::min(target.size.x, (int)(uint64)width/2), image.size.y/2));
     const Image8 source = cropRef(image, int2(-scroll.offset.x, 0), int2(width, image.size.y));
     bilinear(targetArea, toImage(source));
     /*bilinear(cropRef(target, int2(0, (target.size.y-height-image.size.y)/2), int2(::min(target.size.x, (int)(uint64)width/2), image.size.y/2)),
              toImage(cropRef(image, int2(-scroll.offset.x, 0), int2(width, image.size.y)))); // FIXME: bilinear8*/
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
     keyboard.render(cropRef(target, int2(0, /*image.size.y/2*/target.size.y-height), int2(target.size.x, /*target.size.y-image.size.y/2*/height)));
     renderTime.stop();
     log(strD(resampleTime, totalTime), strD(renderTime, totalTime));
     return shared<Graphics>();
 }
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) override {
return scroll.ScrollArea::mouseEvent(cursor, size, event, button, focus);
 }
 bool keyPress(Key key, Modifiers modifiers) override { return scroll.ScrollArea::keyPress(key, modifiers); }
} test;
