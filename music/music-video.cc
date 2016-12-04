#include "MusicXML.h"
#include "abc.h"
#include "midi.h"
#include "sheet.h"
#include "window.h"
#include "interface.h"
#include "ui/layout.h"
#include "keyboard.h"
#include "fret.h"
#include "audio.h"
#include "asound.h"
#include "ui/render.h"
#include "time.h"
#include "parallel.h"
#include "fft.h"
#include "biquad.h"
#include "video.h"
#include "encoder.h"
#define SAMPLER 0
#if SAMPLER
#include "sampler.h"
#endif

/// Converts signs to notes
MidiNotes notes(ref<Sign> signs, uint ticksPerQuarter, 	ref<float2> staffGains = {}) {
 assert_(signs);
 MidiNotes notes;
 for(Sign sign: signs) {
  if(sign.type==Sign::Metronome) {
   notes.ticksPerSeconds = max(notes.ticksPerSeconds, sign.metronome.perMinute*ticksPerQuarter);
  }
  else if(sign.type == Sign::Note) {
   if(sign.note.tie == Note::NoTie || sign.note.tie == Note::TieStart)
    notes.insertSorted({sign.time*60, sign.note.key(), 64/*FIXME: use dynamics*/, staffGains?staffGains[sign.staff]:1});
   if(sign.note.tie == Note::NoTie || sign.note.tie == Note::TieStop)
    notes.insertSorted({(sign.time+sign.duration)*60, sign.note.key(), 0});
  }
 }
 assert(notes.last().time >= 0);
 if(!notes.ticksPerSeconds) notes.ticksPerSeconds = 90*ticksPerQuarter; //TODO: default tempo from audio
 assert_(notes.ticksPerSeconds);
 return notes;
}

uint audioStart(string audioFileName) {
 if(!audioFileName) return 0;
 for(FFmpeg file(audioFileName);;) {
  int32 buffer[1024  * file.channels];
  size_t size = file.read32(mref<int32>(buffer, 1024 * file.channels));
  assert_(size, audioFileName, existsFile(audioFileName), file.duration);
  for(size_t i: range(size  * file.channels)) if(abs(buffer[i])>1<<23) return file.audioTime+i;
 }
}

/// Converts MIDI time base to audio sample rate
MidiNotes scale(MidiNotes&& notes, uint targetTicksPerSeconds, int unused start) {
 assert_(notes);
 //assert_(targetTicksPerSeconds);
 //assert_(start==0, start);
 //int offset = start;
 //int offset = start-(int64)notes.first().time*targetTicksPerSeconds/notes.ticksPerSeconds;
 //int offset = 0;
 int offset = -(int64)notes.first().time*targetTicksPerSeconds/notes.ticksPerSeconds;
 for(MidiNote& note: notes) {
  note.time = offset + (int64)note.time*targetTicksPerSeconds/notes.ticksPerSeconds;
 }
 notes.ticksPerSeconds = targetTicksPerSeconds;
 return move(notes);
}

struct Peak { int time; uint key; float value; };
template<> inline String str(const Peak& o) { return strKey(0, o.key); } //str(o.time/48000, o.key, str(o.value, 2u)); }
bool operator <(const Peak& a, const Peak& b) { return a.value > b.value; } // Decreasing order
typedef array<Peak> Bin;

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }

struct Synchronizer : Widget {
 map<uint, float>& measureBars;
 struct Bar { float x; bgr3f color; };
 array<Bar> bars;
 int currentTime = 0;
 Synchronizer(Audio audio, MidiNotes& notes, ref<Sign> signs, map<uint, float>& measureBars) : measureBars(measureBars) {
  //log(notes.ticksPerSeconds, notes.last().time, audio.rate, audio.size);
  array<Bin> P; // peaks
  if(audio) {
   assert_(notes.ticksPerSeconds = audio.rate);

   uint firstKey = 21+85, lastKey = 0;
   for(MidiNote& note: notes) {
    firstKey = min(firstKey, note.key);
    lastKey = max(lastKey, note.key);
   }
   uint keyCount = lastKey+1-firstKey;

   size_t T = audio.size/audio.channels; // Total sample count
#if 0 // FFT
   size_t N = 8192;  // Frame size: 48KHz * 2 (Nyquist) * 2 (window) / frameSize ~ 24Hz~A0
   size_t h = 2048; // Hop size: 48KHz * 60 s/min / 4 b/q / hopSize / 2 (Nyquist) ~ 175 bpm
   FFT fft(N);
   size_t frameCount = (T-N)/h;
   buffer<float> keyOnsetFunctions(keyCount*frameCount); keyOnsetFunctions.clear(0);
   int firstK = floor(keyToPitch(firstKey)*N/audio.rate);
   int lastK = ceil(keyToPitch(lastKey)*N/audio.rate);
   for(size_t frameIndex: range(frameCount)) {
    ref<float> X = audio.slice(frameIndex*h*audio.channels, N*audio.channels);
    if(audio.channels==1) for(size_t i: range(N)) fft.windowed[i] = fft.window[i] * X[i];
    else if(audio.channels==2) for(size_t i: range(N)) fft.windowed[i] = fft.window[i] * (X[i*2+0]+X[i*2+1])/2;
    else error(audio.channels, "channels");
    fft.transform();
    for(size_t k: range(firstK, lastK+1)) { // For each bin
     // For each intersected key
     for(size_t key: range(max(firstKey, uint(round(pitchToKey(k*audio.rate/N)))), round(pitchToKey((k+1)*audio.rate/N))))
      keyOnsetFunctions[(key-firstKey)*frameCount+frameIndex] += fft.spectrum[k];
    }
    if(frameIndex>0) for(size_t key: range(keyCount)) { // Differentiates (after binning to keys to be more robust against frequency oscillations)
     keyOnsetFunctions[key*frameCount+frameIndex] =
       max(0.f, keyOnsetFunctions[key*frameCount+frameIndex] - keyOnsetFunctions[key*frameCount+frameIndex-1]);
    }
   }
#else // Filter bank
   const size_t h = 2400; // 48000/2400 = 20Hz (50ms)
   size_t frameCount = T/h;
   buffer<float> keyOnsetFunctions(keyCount*frameCount);
   log("Filter...");
   Time time;
   chunk_parallel(keyCount, [&](uint, size_t key) {
    BandPass filter(keyToPitch(firstKey+key)/audio.rate, /*Q=*/25);
    mref<float> F = keyOnsetFunctions.slice(key*frameCount, frameCount);
    for(size_t frameIndex: range(frameCount)) {
     double sum = 0;
     ref<float> X = audio.slice(frameIndex*h*audio.channels, h*audio.channels);
     if(audio.channels==1) for(size_t i: range(h)) sum += filter(X[i]);
     else if(audio.channels==2) for(size_t i: range(h)) sum += filter((X[i*2+0]+X[i*2+1])/2);
     else error(audio.channels, "channels");
     F[frameIndex] = max(0.f, float(sum / h) - (frameIndex>0 ? F[frameIndex-1] : 0));
    }
   });
   log(time);
#endif
   //buffer<float> meanF(keyCount);
   /// Normalizes onset function
   for(size_t key: range(keyCount)) {
    mref<float> f = keyOnsetFunctions.slice(key*frameCount, frameCount);
    float SSQ = 0;
    for(float& v: f) SSQ += sq(v);
    float deviation = sqrt(SSQ / frameCount);
    if(deviation) for(float& v: f) v /= deviation;
    //float sum = 0; for(float& v: f) sum += v; meanF[key] = sum / frameCount;
   }

   /// Selects onset peaks
   {const int w = 2;
    const int before = 9, after = 3; //const int W = 3, m = 3;
    for(int i: range(frameCount)) {
     array<Peak> chord;
     for(uint key: range(keyCount)) {
      ref<float> f = keyOnsetFunctions.slice(key*frameCount, frameCount);
      bool localMaximum = true;
      for(int k: range(max(0, i-w), min(int(f.size)-1, i+w+1))) {
       if(k==i) continue;
       if(!(f[i] > f[k])) localMaximum=false;
      }
      if(localMaximum) {
       double sum = 0;
       //for(size_t k: range(max(0, i-m*W), min(int(f.size)-1, i+W+1))) sum += f[k];
       for(size_t k: range(max(0, i-before), min(int(f.size)-1, i+after+1))) sum += f[k];
       double mean = sum / (min(int(f.size)*1, i+after+1) - max(0, i-before));
       //const double threshold = 1./4; // FFT
       const double threshold = 1; // Filter
       if(f[i] > mean + threshold)
        chord.insertSorted(Peak{int(i*h)/*-peakTimeOrigin*/, firstKey+key, f[i]});
      }
     }
     if(chord) P.append(move(chord));
    }
   }
   log(P);

   /// Collects MIDI chords following MusicXML at the same time to cluster (assumes performance is correctly ordered)
   array<Bin> S; // MIDI
   buffer<MidiNote> onsets = filter(notes, [](MidiNote o){return o.velocity==0;});
   for(size_t index = 0; index < onsets.size;) {
    array<Peak> chord;
    int64 time = onsets[index].time;
    while(index < onsets.size && onsets[index].time < time+uint64(h)) {
     if(chord) assert_(int(time)/*-scoreTimeOrigin*/-chord[0].time < 24000, onsets[index].time, chord[0].time);
     chord.append(Peak{int(onsets[index].time)/*-scoreTimeOrigin*/, onsets[index].key, (float)onsets[index].velocity});
     index++;
    }
    S.append(move(chord));
   }

   /// Synchronizes audio and score using dynamic time warping
   size_t m = S.size, n = P.size;
   assert_(m < n, m, n);

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
    for(Peak s: S[i]) for(Peak p: P[j]) d += (s.key==p.key || s.key==p.key-12 || s.key==p.key-12-7) * p.value;
    // Evaluates best cumulative score to an alignment point (i,j)
    D(i,j) = max((float[]){
                  j+1==n?0:D(i,j+1), // Ignores peak j
                  i+1==m?0:D(i+1, j), // Ignores chord i
                  ((i+1==m||j+1==n)?0:D(i+1,j+1)) + d // Matches chord i with peak j
                 });
   };

   // Evaluates _strictly_ monotonous map by walking back the best path on the cumulative score matrix
   int globalOffset = 0;
   // Forward scan (chronologic for clearer midi time correlation logic)
   size_t i = 0, j = 0;
   size_t midiIndex = 0;
   while(i<m && j<n) {
    int localOffset = (int(P[j][0].time) - int(notes[midiIndex].time)) - globalOffset; // >0: late, <0: early
    const int limitDelay = notes.ticksPerSeconds, limitAdvance = notes.ticksPerSeconds; //notes.ticksPerSeconds*4;
    if(j+1<n && ((D(i,j) == D(i,j+1) && (i==0 || localOffset < limitDelay)) || (i && localOffset < -limitAdvance))) {
    //if(j+1<n && D(i,j) == D(i,j+1)) {
     j++;
    } else {
     //log(P[j], onsets[i], notes[midiIndex]);
     //globalOffset = P[0][0].time;
     globalOffset = int(P[j][0].time) - int(notes[midiIndex].time);
     for(size_t unused count: range(S[i].size)) {
      assert_(notes[midiIndex].velocity);
      notes[midiIndex].time += globalOffset; // Shifts all events until next chord, by the offset of last match
      midiIndex++;
      while(midiIndex < notes.size && !notes[midiIndex].velocity) {
       notes[midiIndex].time += globalOffset;
       midiIndex++;
      }
     }
     i++; j++;
    }
   }
  }

  { // Sets measure times to MIDI times
   buffer<MidiNote> onsets = filter(notes, [](MidiNote o){return o.velocity==0;});
   //assert_(onsets.size == signs.size || !signs.size, onsets.size, signs.size); FIXME
   assert_(onsets.size >= signs.size, onsets.size, signs.size);
   //assert_(onsets.size*2 == signs.size || !signs.size, onsets.size, signs.size);
   size_t index = 0;
   for(size_t measureIndex: range(measureBars.size())) {
    while(index < signs.size) {
     if(signs[index].note.measureIndex != invalid) { // FIXME
      assert_(signs[index].note.measureIndex != invalid);
      if(signs[index].note.measureIndex >= measureIndex) break;
     }
     index++;
    }
    if(index == signs.size) {
     if(measureIndex < measureBars.size()-1) break; // FIXME
     assert_(measureIndex == measureBars.size()-1, measureIndex, measureBars.size()-1);
     measureBars.keys[measureIndex] = onsets.last().time; // Last measure (FIXME: last release time)
    } else {
     if(signs[index].note.measureIndex != measureIndex) { // Empty measure
      //log("Empty measure", signs[index].note.measureIndex, measureIndex);
      assert_(index>0, index, measureIndex, signs[index].note.measureIndex);
      measureBars.keys[measureIndex] = onsets[index-1].time; // FIXME: should be onsets[index].time - measureTime[index]
     } else {
      measureBars.keys[measureIndex] = onsets[index].time;
     }
    }
   }
  }

  // MIDI visualization (synchronized to sheet time)
  {size_t midiIndex = 0;
   auto onsets = apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.time;});
   for(int index: range(measureBars.size()-1)) {
    uint t0 = measureBars.keys[index];
    float x0 = measureBars.values[index];
    uint t1 = measureBars.keys[index+1];
    float x1 = measureBars.values[index+1];
    while(midiIndex<onsets.size && t0 <= onsets[midiIndex] && onsets[midiIndex] < t1) {
     assert_(t1>t0);
     float x = x0+(x1-x0)*(onsets[midiIndex]-t0)/(t1-t0);
     bars.append(Bar{x, blue});
     midiIndex++;
    }
   }
  }

  // Beat visualization (synchronized to sheet time)
  if(P) {
   size_t peakIndex = 0;
   {// Before first measure
    int t0 = 0;
    assert_(P[peakIndex][0].time >= 0);
    float x0 = 0;
    int t1 = measureBars.keys[0];
    float x1 = measureBars.values[1];
    while(peakIndex<P.size && t0 <= /*peakTimeOrigin+*/P[peakIndex][0].time && /*peakTimeOrigin+*/P[peakIndex][0].time < t1) {
     assert_(t1>t0);
     float x = x0+(x1-x0)*(/*peakTimeOrigin+*/P[peakIndex][0].time-t0)/(t1-t0);
     bars.append(Bar{x, red});
     peakIndex++;
    }
   }
   for(int index: range(measureBars.size()-1)) {
    int t0 = measureBars.keys[index];
    float x0 = measureBars.values[index];
    int t1 = measureBars.keys[index+1];
    float x1 = measureBars.values[index+1];
    while(peakIndex<P.size && t0 <= /*peakTimeOrigin+*/P[peakIndex][0].time && /*peakTimeOrigin+*/P[peakIndex][0].time < t1) {
     assert_(t1>t0);
     float x = x0+(x1-x0)*(/*peakTimeOrigin+*/P[peakIndex][0].time-t0)/(t1-t0);
     bars.append(Bar{x, red});
     peakIndex++;
    }
   }
  }

  for(float x: measureBars.values) bars.append(Bar{x, green});
 }
 vec2 sizeHint(vec2) override { return vec2(measureBars.values.last(), 32); }
 shared<Graphics> graphics(vec2 size) override {
  //assert_(size.x > 1050, size, sizeHint(size));
  shared<Graphics> graphics;
  for(Bar bar: bars) graphics->fills.append(vec2(bar.x, 0), vec2(2, size.y), bar.color, 1.f/2);
  for(int index: range(measureBars.size()-1)) {
   int t0 = measureBars.keys[index];
   float x0 = measureBars.values[index];
   int t1 = measureBars.keys[index+1];
   float x1 = measureBars.values[index+1];
   if(t0 <= currentTime && currentTime < t1) {
    assert_(t1>t0);
    float x = x0+(x1-x0)*(currentTime-t0)/(t1-t0);
    graphics->fills.append(vec2(x, 0), vec2(2, size.y));
   }
  }
  return graphics;
 }
};

void scale(const Image& target, const Image& source, int num, int den) {
 if(den==1)
  for(size_t i: range(target.ref::size)) {
   target[i] = byte4(min(0xFFu, uint(source[i].b)*num), min(0xFFu, uint(source[i].g)*num), min(0xFFu, uint(source[i].r)*num), 0xFF);
  }
 else
  for(size_t i: range(target.ref::size)) {
   target[i] = byte4(min(0xFFu, uint(source[i].b)*num/den), min(0xFFu, uint(source[i].g)*num/den), min(0xFFu, uint(source[i].r)*num/den), 0xFF);
  }
}
Image scale(const Image& source, int num, int den) { Image target(source.size); scale(target, source, num, den); return target; }

uint mean(const Image& image) {
 uint sum = 0;
 for(byte4 p: image) sum += p.b + p.g + p.r;
 return sum / image.ref::size;
}

struct Music : Widget {
 String findTitle() {
  String title;
  if(arguments()) title = copyRef(arguments()[0]);
  else for(string file: currentWorkingDirectory().list(Files)) {
   if(!title) title=copyRef(section(file,'.'));
   assert_(section(file,'.')==title, title, section(file,'.'), "Multiple title in folder. Executable called without specifying which title as a command line argument");
  }
  if(title.contains('.')) title = copyRef(section(title,'.'));
  return title;
 }

 // Name
 String name = findTitle();
 // Files
 String audioFileName = arguments().contains("noaudio") ? ""__ : (arguments().contains("novideo") || !existsFile(name+".mp4") ? name+".mp3" : name+".mp4"); //+".mkv"; //".mp4";
 String videoFile = arguments().contains("novideo") ? ""__ : name+".mp4"; //".mkv"; //.mp4";

 // Audio
 unique<FFmpeg> audioFile = audioFileName ? unique<FFmpeg>(audioFileName) : nullptr;
 const bool encode = arguments().contains("encode") || arguments().contains("export");
#if SAMPLER
 // Sampler
 Thread decodeThread;
 Sampler sampler {"Piano/Piano.sfz"_, 1024, {this, &Music::timeChanged}, encode ? mainThread : decodeThread};
#else
 struct { int rate=0; } sampler;
#endif
 // MusicXML
 MusicXML xml = existsFile(name+".xml"_) ? readFile(name+".xml"_) : MusicXML();
 // ABC
 ABC abc = existsFile(name+".abc"_) ? ABC(readFile(name+".abc"_)) : ABC();
 // MIDI
 MidiFile midi = existsFile(name+".mid"_) ? MidiFile(readFile(name+".mid"_)) : MidiFile(); // if used: midi.signs are scaled in synchronizer

 const mref<Sign> signs = xml ? xml.signs : abc ? abc.signs : midi.signs;
 const uint ticksPerBeat = xml ? xml.divisions : abc ? abc.ticksPerBeat : 1000000;

 MidiNotes notes = ::scale(midi ? copy(midi.notes) : ::notes(signs, ticksPerBeat), audioFile ? audioFile->audioFrameRate : sampler.rate, audioStart(audioFileName));
 Sheet sheet {signs, ticksPerBeat, 0, 6, filter(notes, [](MidiNote o){return o.velocity==0;})};
 Synchronizer synchronizer {audioFileName&&!midi?decodeAudio(audioFileName):Audio(), notes, sheet.midiToSign, sheet.measureBars};

 // Video
 Decoder video = videoFile && existsFile(videoFile) ? Decoder(videoFile) : Decoder();

 // State
 bool failed = sheet.firstSynchronizationFailureChordIndex != invalid;
 bool running = !arguments().contains("pause") && !failed;
 /*bool keyboardView = false; //videoFile ? endsWith(videoFile, ".mkv") : false;
 bool rotate = keyboardView;
 bool crop = keyboardView;
 bool scale = keyboardView;
 int resize = keyboardView ? 4 : 1;*/

 // View
 GraphicsWidget system {move(sheet.pages[0])};
 //Scroll<VBox> scroll {{&system, &synchronizer}}; // 1/3 ~ 240
 Scroll<VBox> scroll {{&system}}; // 1/3 ~ 240
 ImageView videoView; // 1/2 ~ 360
 Keyboard keyboard; // 1/6 ~ 120
 Fret fret;
 VBox widget {{&scroll, &videoView, &fret/*, &keyboard*/}};

 // Highlighting
 map<uint, Sign> active; // Maps active keys to notes (indices)
 uint midiIndex = 0, noteIndex = 0;

 // Video preview
 unique<Window> window = nullptr;

 // Audio preview
 Thread audioThread;
 AudioOutput audio = {{this, &Music::read32}, audioThread};

 size_t read32(mref<int2> output) {
  if(audioFile) {
   if(audioFile->channels == audio.channels) return audioFile->read32(mcast<int>(output));
   else if(audioFile->channels == 1 && audio.channels==2) {
    int buffer[output.size];
    audioFile->read32(mref<int>(buffer, output.size));
    for(size_t i: range(output.size)) output[i] = buffer[i];
    return output.size;
   } else error(audioFile->channels);
  } else {
#if SAMPLER
   assert_(sampler.channels == audio.channels);
   assert_(sampler.rate == audio.rate);
   return sampler.read32(output);
#else
   error("UNIMPL Sampler");
   return 0;
#endif
  }
 }

#if SAMPLER
 /// Adds new notes to be played (called in audio thread by sampler)
 uint samplerMidiIndex = 0;
 void timeChanged(uint time) {
  while(samplerMidiIndex < notes.size && (int64)notes[samplerMidiIndex].time*sampler.rate <= (int64)time*notes.ticksPerSeconds) {
   auto note = notes[samplerMidiIndex];
   sampler.noteEvent(note.key, note.velocity);//, note.gain);
   samplerMidiIndex++;
  }
 }
#endif

 bool follow(int64 timeNum, int64 timeDen, vec2 size, bool unused preview=true) {
  assert_(timeDen);
  assert_(timeNum >= 0, timeNum);
  constexpr int staffCount = 1;
  bool contentChanged = false;
  for(;midiIndex < notes.size && (int64)notes[midiIndex].time*timeDen <= timeNum*(int64)notes.ticksPerSeconds; midiIndex++) {
   MidiNote note = notes[midiIndex];
   if(note.velocity) {
    assert_(noteIndex < sheet.midiToSign.size, noteIndex, sheet.midiToSign.size);
    Sign sign = sheet.midiToSign[noteIndex];
    if(sign.type == Sign::Note) {
     active.insertMulti(note.key, sign);
     (sign.staff?keyboard.left:keyboard.right).append( sign.note.key() );
     fret.active.insertMulti(note.key, sign);
     if(sign.note.pageIndex != invalid && sign.note.glyphIndex[0] != invalid) {
      assert_(sign.note.pageIndex == 0);
      for(size_t index: ref<size_t>(sign.note.glyphIndex)) if(index!=invalid) system.glyphs[index].color = (sign.staff?red:(staffCount==1?blue:green));
      contentChanged = true;
     }
    }
    // Updates next notes
    fret.measure.clear();
    size_t firstSignIndex = sign.note.signIndex;
    assert_(firstSignIndex != invalid);
    bool firstNote = true;
    for(size_t signIndex: range(firstSignIndex, signs.size)) {
     assert_(signIndex < signs.size, signIndex);
     const Sign& sign = signs[signIndex];
     if(sign.type == Sign::Note) {
      fret.measure.insertMulti(sign.note.key(), sign);
      if(!firstNote && sign.note.finger) break; // Shows all notes until next hand position change
      firstNote = false;
     }
     //if(sign.type == Sign::Measure) break;
    }
    noteIndex++;
   }
   else if(!note.velocity && active.contains(note.key)) {
    while(active.contains(note.key)) {
     Sign sign = active.take(note.key);
     fret.active.take(note.key);
     //if(fret.measure.contains(note.key)) fret.measure.remove(note.key);
     (sign.staff?keyboard.left:keyboard.right).remove( sign.note.key() );
     if(sign.note.pageIndex != invalid && sign.note.glyphIndex[0] != invalid) {
      assert_(sign.note.pageIndex == 0);
      for(size_t index: ref<size_t>(sign.note.glyphIndex)) if(index!=invalid)  system.glyphs[index].color = black;
     }
     // Updates next notes
     fret.measure.clear();
     size_t firstSignIndex = sign.note.signIndex+1;
     assert_(firstSignIndex != invalid);
     bool firstNote = true;
     for(size_t signIndex: range(firstSignIndex, signs.size)) {
      assert_(signIndex < signs.size, signIndex);
      const Sign& sign = signs[signIndex];
      if(sign.type == Sign::Note) {
       fret.measure.insertMulti(sign.note.key(), sign);
       if(!firstNote && sign.note.finger) break; // Shows all notes until next hand position change
       firstNote = false;
      }
      //if(sign.type == Sign::Measure) break;
     }
     contentChanged = true;
    }
   }
  }

  int64 t = (int64)timeNum*notes.ticksPerSeconds;
  float previousOffset = scroll.offset.x;
  // Cardinal cubic B-Spline
  for(int index: range(sheet.measureBars.size()-1)) {
   int64 t1 = (int64)sheet.measureBars.keys[index]*timeDen;
   int64 t2 = (int64)sheet.measureBars.keys[index+1]*timeDen;
   if(t1 <= t && t < t2) {
    double f = double(t-t1)/double(t2-t1);
    double w[4] = { 1./6 * cb(1-f), 2./3 - 1./2 * sq(f)*(2-f), 2./3 - 1./2 * sq(1-f)*(2-(1-f)), 1./6 * cb(f) };
    auto X = [&](int index) { return clamp(0.f, sheet.measureBars.values[clamp<int>(0, index, sheet.measureBars.values.size-1)] - size.x/2,
       abs(system.sizeHint(size).x)-size.x); };
    float newOffset = round( w[0]*X(index-1) + w[1]*X(index) + w[2]*X(index+1) + w[3]*X(index+2) );
    if(newOffset >= -scroll.offset.x) scroll.offset.x = -newOffset;
    break;
   }
  }
  if(previousOffset != scroll.offset.x) contentChanged = true;
  synchronizer.currentTime = (int64)timeNum*notes.ticksPerSeconds/timeDen;
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
  return contentChanged;
 }

 int seek(int64 time) {
  if(audioFile) {
   assert_(notes.ticksPerSeconds == audioFile->audioFrameRate);
   audioFile->seek(time); //FIXME: return actual frame time
   //buffer<int32> frame(1024); while(audioFile->audioTime < time) audioFile->read32(frame);
  } /*else {
            sampler.audioTime = time*sampler.rate/notes.ticksPerSeconds;
            while(samplerMidiIndex < notes.size && notes[samplerMidiIndex].time*sampler.rate < time*notes.ticksPerSeconds) samplerMidiIndex++;
        }*/
  /*if(video) {
            //video.seek(time * video.videoFrameRate / notes.ticksPerSeconds); //FIXME
            assert_(notes.ticksPerSeconds == audioFile->audioFrameRate);
            while((video.videoTime+video.timeNum)*notes.ticksPerSeconds <= time*video.timeDen) video.read();
            return video.videoTime;
        }*/
  return 0;
 }

 Music() {
  if(!abc) {
   array<char> s;
   int64 lastTime = 0;
   int time = 0; // Beat time
   int duration = 0;
   bool tuplet = false;
   for(const Sign& sign: signs) {
    if(int64(sign.time) > lastTime) {
     s.append(' ');
     time += duration;
     duration = 0;
    }
    lastTime = sign.time;
    /**/  if(sign.type==Sign::Measure) {
     if((time+6)%(12*4)==0) {
      s.append(',');
      time += 6;
     }
     assert_(time%(12*4)==0, time, time%(12*4), s);
     s.append("\n");
    }
    else if(sign.type==Sign::Rest) {
     s.append(str(sign));
     if(sign.rest.value == Sixteenth) time += 3;
     else if(sign.rest.value == Eighth) time += 6;
     else if(sign.rest.value == Quarter) time += 12;
     else error(int(sign.rest.value));
     log(time);
    }
    else if(sign.type==Sign::Note) {
     if(!tuplet && sign.note.durationCoefficientDen > 1) {
      s.append('[');
      tuplet = true;
     }
     if(tuplet) assert_(sign.note.durationCoefficientDen > 1, sign.note.durationCoefficientDen, sign.note.durationCoefficientNum, s);
     s.append(str(sign));
     if(sign.note.value == Sixteenth) duration = 3;
     else if(sign.note.value == Eighth) duration = 6;
     else if(sign.note.value == Quarter) duration = 12;
     else error(int(sign.note.value));
     if(tuplet) duration = duration*2/3;
     }
    else if(sign.type==Sign::Clef) {
     //assert(sign.clef.clefSign == GClef);
     s.append(str(sign)+'\n');
    }
    else if(sign.type==Sign::TimeSignature) {
     //assert(sign.timeSignature.beats == 4 && sign.timeSignature.beatUnit == 4);
     s.append(str(sign)+'\n');
    }
    else if(sign.type==Sign::Metronome) {
     //assert(sign.metronome.beatUnit == Quarter && sign.metronome.perMinute == 106);
     s.append(str(sign)+'\n');
    }
    else if(sign.type==Sign::Tuplet) {
     assert_(tuplet);
     s.append(']');
     tuplet = false;
    }
    else error(int(sign.type), sign);
   }
   log(s);
   writeFile(name+".abc", s);
   return;
  }

  scroll.horizontal=true, scroll.vertical=false, scroll.scrollbar = true;
  if(failed) { // Seeks to first synchronization failure
   size_t measureIndex = 0;
   for(;measureIndex < sheet.measureToChord.size; measureIndex++)
    if(sheet.measureToChord[measureIndex]>=sheet.firstSynchronizationFailureChordIndex) break;
   scroll.offset.x = -sheet.measureBars.values[max<int>(0, measureIndex-3)];
  }
  if(encode) { // Encode
   assert_(!failed);

   Encoder encoder {name+".tutorial.mp4"_};
   //encoder.setH264(int2(720, 480), 0/*1000/1001*30*/);
   encoder.setH264(int2(854, 480), 60);
   //encoder.setH264(int2(1280,/*720*/240), 30/*60*/);
   //encoder.setH264(int2(1280,widget.sizeHint(vec2(1280,720)).y), 60);
   if(audioFile && (audioFile->codec==FFmpeg::AAC || audioFile->codec==FFmpeg::MP3)) encoder.setAudio(audioFile);
   else error("Unknown codec");//if(audioFile) encoder.setAAC(2 /*Youtube requires stereo*/, audioFile->audioFrameRate);
   encoder.open();

   uint videoTime = 0;

   Time renderTime, videoEncodeTime, totalTime;
   totalTime.start();
   for(int lastReport=0;;) {
    //assert_(encoder.audioStream->time_base.num == 1);
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
     while((int64)encoder.videoTime*encoder.audioFrameRate*encoder.videoFrameRateDen <= (int64)encoder.audioTime*encoder.videoFrameRateNum) {
      follow(videoTime*encoder.videoFrameRateDen, encoder.videoFrameRateNum, vec2(encoder.size), false);
      renderTime.start();
      Image target (encoder.size);
      target.clear(0xFF);
      ::render(target, widget.graphics(vec2(target.size), Rect(vec2(target.size))));
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
    if(encoder.videoFrameRateNum) timeTicks = (uint64)videoTime*encoder.videoFrameRateDen*notes.ticksPerSeconds/encoder.videoFrameRateNum;
    else if(encoder.audioFrameRate) timeTicks = (uint64)encoder.audioTime*notes.ticksPerSeconds/encoder.audioFrameRate;
    else error("");
    //buffer<uint> onsets = apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.time;});
    //uint64 durationTicks = onsets.last() + 4*notes.ticksPerSeconds;
    uint64 durationTicks = notes.last().time;
    int percent = round(100.*timeTicks/durationTicks);
    if(percent!=lastReport) {
     log(str(percent, 2u)+"%", "Render", strD(renderTime, totalTime), "Encode", strD(videoEncodeTime, totalTime)
         /*,int(round((float)totalTime*((float)durationTicks/timeTicks-1))), "/", int(round((float)totalTime/timeTicks*durationTicks)), "s"*/);
     lastReport=percent;
    }
    if(timeTicks >= durationTicks) break;
    if(video && video.videoTime >= video.duration) break;
    //if(timeTicks > 8*notes.ticksPerSeconds) break; // DEBUG
   }
   log("Done");
  } else { // Preview
   //window = ::window(this, int2(854, 480));
   window = ::window(this, int2(1280,720));
   window->backgroundColor = white;
   window->show();
   if(running && audioFile && playbackDeviceAvailable()) {
#if SAMPLER
    if(sampler) decodeThread.spawn();
#endif
    audio.start(audioFile ? audioFile->audioFrameRate : /*sampler.rate*/0, audioFile ? 1024 : /*sampler.periodSize*/0, 32, 2);
    assert_(audio.rate == (audioFile ? audioFile->audioFrameRate : /*sampler.rate*/0), audio.rate);
    //seek(audioFile->duration/3);
    audioThread.spawn();
   } else running = false;
  }
 }

 vec2 sizeHint(vec2 size) override { return running ? widget.sizeHint(size) : scroll.ScrollArea::sizeHint(size); }
 shared<Graphics> graphics(vec2 size) override {
  if(running /*&& video.videoTime < video.duration*/) {
   if(audioFile) {
    //log(audioFile->audioTime/float(audioFile->audioFrameRate));
    follow(audioFile->audioTime, audioFile->audioFrameRate, vec2(window->size));
   }
#if SAMPLER
   else follow(sampler.audioTime, sampler.rate, vec2(window->size));
#endif
   window->render();
  }
  return running ? widget.graphics(size, Rect(size)) : scroll.ScrollArea::graphics(size);
 }
 bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) override {
  return scroll.ScrollArea::mouseEvent(cursor, size, event, button, focus);
 }
 bool keyPress(Key key, Modifiers modifiers) override { return scroll.ScrollArea::keyPress(key, modifiers); }
} app;